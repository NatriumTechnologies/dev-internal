#include "synapse/shm.hpp"
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <cerrno>
#endif

namespace synapse {

SharedMemory::SharedMemory() noexcept = default;

SharedMemory::~SharedMemory() {
    close();
}

SharedMemory::SharedMemory(SharedMemory&& other) noexcept
    : name_(std::move(other.name_)),
      size_(other.size_),
      mapped_ptr_(other.mapped_ptr_),
      handle_(other.handle_),
      is_creator_(other.is_creator_) {
    other.size_ = 0;
    other.mapped_ptr_ = nullptr;
    other.handle_ = nullptr;
    other.is_creator_ = false;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept {
    if (this != &other) {
        close();
        name_ = std::move(other.name_);
        size_ = other.size_;
        mapped_ptr_ = other.mapped_ptr_;
        handle_ = other.handle_;
        is_creator_ = other.is_creator_;

        other.size_ = 0;
        other.mapped_ptr_ = nullptr;
        other.handle_ = nullptr;
        other.is_creator_ = false;
    }
    return *this;
}

Result<SharedMemory> SharedMemory::open(
    std::string_view name,
    size_t size,
    ShmAccess access
) {
    if (name.empty() || size == 0) {
        return Status::InvalidArgument;
    }

    SharedMemory shm;
    shm.name_ = std::string(name);
    shm.size_ = size;

#if defined(_WIN32)
    std::wstring wname;
    if (shm.name_.rfind("Local\\", 0) != 0 && shm.name_.rfind("Global\\", 0) != 0) {
        wname = L"Local\\";
    }
    wname.append(shm.name_.begin(), shm.name_.end());

    HANDLE h_map = nullptr;
    bool created = false;

    if (access == ShmAccess::OpenExisting) {
        h_map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
        if (!h_map) {
            return Status::ShmCreationFailed;
        }
    } else {
        const DWORD high_size = static_cast<DWORD>((static_cast<uint64_t>(size) >> 32) & 0xFFFFFFFFu);
        const DWORD low_size = static_cast<DWORD>(size & 0xFFFFFFFFu);

        h_map = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            high_size,
            low_size,
            wname.c_str()
        );

        if (!h_map) {
            return Status::ShmCreationFailed;
        }

        const DWORD last_err = GetLastError();
        if (last_err == ERROR_ALREADY_EXISTS) {
            if (access == ShmAccess::CreateOnly) {
                CloseHandle(h_map);
                return Status::ShmCreationFailed;
            }
            created = false;
        } else {
            created = true;
        }
    }

    void* ptr = MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!ptr) {
        CloseHandle(h_map);
        return Status::ShmMapFailed;
    }

    shm.handle_ = h_map;
    shm.mapped_ptr_ = ptr;
    shm.is_creator_ = created;
    return shm;

#else
    std::string posix_name;
    if (shm.name_.front() != '/') {
        posix_name = "/" + shm.name_;
    } else {
        posix_name = shm.name_;
    }

    int oflags = 0;
    bool created = false;

    if (access == ShmAccess::OpenExisting) {
        oflags = O_RDWR;
    } else if (access == ShmAccess::CreateOnly) {
        oflags = O_CREAT | O_EXCL | O_RDWR;
    } else {
        oflags = O_CREAT | O_RDWR;
    }

    int fd = shm_open(posix_name.c_str(), oflags, 0666);
    if (fd < 0) {
        return Status::ShmCreationFailed;
    }

    struct stat sb{};
    if (fstat(fd, &sb) == 0 && sb.st_size == 0) {
        if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
            ::close(fd);
            return Status::ShmCreationFailed;
        }
        created = true;
    }

    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        return Status::ShmMapFailed;
    }

    shm.handle_ = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    shm.mapped_ptr_ = ptr;
    shm.is_creator_ = created;
    return shm;
#endif
}

void SharedMemory::close() noexcept {
    if (mapped_ptr_) {
#if defined(_WIN32)
        UnmapViewOfFile(mapped_ptr_);
#else
        munmap(mapped_ptr_, size_);
#endif
        mapped_ptr_ = nullptr;
    }

    if (handle_) {
#if defined(_WIN32)
        CloseHandle(static_cast<HANDLE>(handle_));
#else
        const int fd = static_cast<int>(reinterpret_cast<intptr_t>(handle_));
        ::close(fd);
        if (is_creator_) {
            std::string posix_name = (name_.front() == '/') ? name_ : ("/" + name_);
            shm_unlink(posix_name.c_str());
        }
#endif
        handle_ = nullptr;
    }

    size_ = 0;
    is_creator_ = false;
}

} // namespace synapse

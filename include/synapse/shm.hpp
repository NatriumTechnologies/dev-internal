#pragma once

#include "synapse/common.hpp"
#include <string>
#include <string_view>
#include <memory>

namespace synapse {

enum class ShmAccess {
    CreateOrOpen,
    CreateOnly,
    OpenExisting
};

class SharedMemory {
public:
    SharedMemory() noexcept;
    ~SharedMemory();

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept;
    SharedMemory& operator=(SharedMemory&& other) noexcept;

    [[nodiscard]] static Result<SharedMemory> open(
        std::string_view name,
        size_t size,
        ShmAccess access = ShmAccess::CreateOrOpen
    );

    void close() noexcept;

    [[nodiscard]] void* data() const noexcept { return mapped_ptr_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool is_creator() const noexcept { return is_creator_; }
    [[nodiscard]] bool is_valid() const noexcept { return mapped_ptr_ != nullptr; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
    size_t size_{0};
    void* mapped_ptr_{nullptr};
    void* handle_{nullptr}; // HANDLE on Windows, file descriptor cast on POSIX
    bool is_creator_{false};
};

} // namespace synapse

cmake_minimum_required(VERSION 3.22)
project(synapse VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SYNAPSE_BUILD_BENCHMARKS "Build Synapse IPC performance benchmarks" ON)
option(SYNAPSE_BUILD_EXAMPLES "Build Synapse IPC sample applications" ON)
option(SYNAPSE_BUILD_TESTS "Build Synapse IPC unit test suite" ON)

find_package(Threads REQUIRED)

add_library(synapse STATIC
    src/shm.cpp
    src/futex.cpp
    src/channel.cpp
)

target_include_directories(synapse PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(synapse PUBLIC
    Threads::Threads
)

if(WIN32)
    target_link_libraries(synapse PUBLIC Synchronization)
    target_compile_definitions(synapse PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(synapse PRIVATE /W4 /permissive-)
    endif()
else()
    target_link_libraries(synapse PUBLIC rt)
    target_compile_options(synapse PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNAPSE_BUILD_BENCHMARKS)
    add_executable(synapse_bench_roundtrip benchmarks/bench_roundtrip.cpp)
    target_link_libraries(synapse_bench_roundtrip PRIVATE synapse)
endif()

if(SYNAPSE_BUILD_EXAMPLES)
    add_executable(synapse_producer examples/producer.cpp)
    target_link_libraries(synapse_producer PRIVATE synapse)

    add_executable(synapse_consumer examples/consumer.cpp)
    target_link_libraries(synapse_consumer PRIVATE synapse)
endif()

if(SYNAPSE_BUILD_TESTS)
    enable_testing()
    add_executable(synapse_test_ring tests/test_ring.cpp)
    target_link_libraries(synapse_test_ring PRIVATE synapse)
    add_test(NAME SynapseTestRing COMMAND synapse_test_ring)
endif()

# Installation rules
include(GNUInstallDirs)
install(TARGETS synapse
    EXPORT SynapseTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/synapse DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

cmake_minimum_required(VERSION 3.20)

project(
        libcortlet_upgradesched
        VERSION 2.0.0
        LANGUAGES C
)

# ---------------------------------------------------------
# C settings
# ---------------------------------------------------------

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------
# Options
# ---------------------------------------------------------

option(
        CORTLET_BUILD_TESTS
        "Build libcortlet-upgradesched tests"
        ON
)

option(
        CORTLET_BUILD_BENCHMARKS
        "Build libcortlet-upgradesched benchmarks"
        ON
)

# ---------------------------------------------------------
# Cargo
# ---------------------------------------------------------

find_program(
        CARGO_EXECUTABLE
        cargo
        REQUIRED
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CARGO_BUILD_MODE "release")
    set(CARGO_BUILD_FLAG "--release")
else()
    set(CARGO_BUILD_MODE "debug")
    set(CARGO_BUILD_FLAG "")
endif()

set(
        CARGO_TARGET_DIR
        "${CMAKE_BINARY_DIR}/cargo-target"
)

set(
        RUST_STATIC_LIBRARY
        "${CARGO_TARGET_DIR}/${CARGO_BUILD_MODE}/libcortlet_upgradesched.a"
)

# ---------------------------------------------------------
# Rust source files
# ---------------------------------------------------------

set(
        RUST_SOURCES
        "${CMAKE_SOURCE_DIR}/src/lib.rs"
        "${CMAKE_SOURCE_DIR}/src/ffi.rs"
        "${CMAKE_SOURCE_DIR}/src/scheduler.rs"
        "${CMAKE_SOURCE_DIR}/src/worker.rs"
        "${CMAKE_SOURCE_DIR}/src/queue.rs"
        "${CMAKE_SOURCE_DIR}/src/steal.rs"
        "${CMAKE_SOURCE_DIR}/src/topology.rs"
)

# ---------------------------------------------------------
# Build Rust core
# ---------------------------------------------------------

add_custom_command(
        OUTPUT
        "${RUST_STATIC_LIBRARY}"

        COMMAND
        ${CMAKE_COMMAND} -E env
        CARGO_TARGET_DIR=${CARGO_TARGET_DIR}
        ${CARGO_EXECUTABLE}
        build
        ${CARGO_BUILD_FLAG}

        WORKING_DIRECTORY
        "${CMAKE_SOURCE_DIR}"

        DEPENDS
        "${CMAKE_SOURCE_DIR}/Cargo.toml"
        ${RUST_SOURCES}

        COMMENT
        "Building libcortlet-upgradesched Rust core"

        VERBATIM
)

add_custom_target(
        cortlet_rust
        ALL
        DEPENDS
        "${RUST_STATIC_LIBRARY}"
)

# ---------------------------------------------------------
# Imported Rust static library target
# ---------------------------------------------------------

add_library(
        cortlet_upgradesched
        STATIC
        IMPORTED
        GLOBAL
)

set_target_properties(
        cortlet_upgradesched
        PROPERTIES
        IMPORTED_LOCATION "${RUST_STATIC_LIBRARY}"
)

add_dependencies(
        cortlet_upgradesched
        cortlet_rust
)

# ---------------------------------------------------------
# Public headers
# ---------------------------------------------------------

set(
        CORTLET_PUBLIC_INCLUDE_DIR
        "${CMAKE_SOURCE_DIR}/include"
)

# ---------------------------------------------------------
# Linux system libraries required by Rust staticlib
# ---------------------------------------------------------

set(
        CORTLET_SYSTEM_LIBS
        pthread
        dl
        m
)

# ---------------------------------------------------------
# Tests
# ---------------------------------------------------------

if(CORTLET_BUILD_TESTS)

    enable_testing()

    # -----------------------------------------------------
    # Basic C ABI test
    # -----------------------------------------------------

    add_executable(
            cortlet_basic_test
            test/basic.c
    )

    target_include_directories(
            cortlet_basic_test
            PRIVATE
            "${CORTLET_PUBLIC_INCLUDE_DIR}"
    )

    target_link_libraries(
            cortlet_basic_test
            PRIVATE
            cortlet_upgradesched
            ${CORTLET_SYSTEM_LIBS}
    )

    add_test(
            NAME
            cortlet_basic

            COMMAND
            cortlet_basic_test
    )

    # -----------------------------------------------------
    # Scheduler test
    # -----------------------------------------------------

    if(EXISTS "${CMAKE_SOURCE_DIR}/test/scheduler.c")

        add_executable(
                cortlet_scheduler_test
                test/scheduler.c
        )

        target_include_directories(
                cortlet_scheduler_test
                PRIVATE
                "${CORTLET_PUBLIC_INCLUDE_DIR}"
        )

        target_link_libraries(
                cortlet_scheduler_test
                PRIVATE
                cortlet_upgradesched
                ${CORTLET_SYSTEM_LIBS}
        )

        add_test(
                NAME
                cortlet_scheduler

                COMMAND
                cortlet_scheduler_test
        )

    endif()

    # -----------------------------------------------------
    # Stress test
    # -----------------------------------------------------

    if(EXISTS "${CMAKE_SOURCE_DIR}/test/stress.c")

        add_executable(
                cortlet_stress_test
                test/stress.c
        )

        target_include_directories(
                cortlet_stress_test
                PRIVATE
                "${CORTLET_PUBLIC_INCLUDE_DIR}"
        )

        target_link_libraries(
                cortlet_stress_test
                PRIVATE
                cortlet_upgradesched
                ${CORTLET_SYSTEM_LIBS}
        )

        add_test(
                NAME
                cortlet_stress

                COMMAND
                cortlet_stress_test
        )

    endif()

endif()

# ---------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------

if(
        CORTLET_BUILD_BENCHMARKS
        AND EXISTS "${CMAKE_SOURCE_DIR}/benchmarks/throughput.c"
)

    add_executable(
            cortlet_benchmark_throughput
            benchmarks/throughput.c
    )

    target_include_directories(
            cortlet_benchmark_throughput
            PRIVATE
            "${CORTLET_PUBLIC_INCLUDE_DIR}"
    )

    target_link_libraries(
            cortlet_benchmark_throughput
            PRIVATE
            cortlet_upgradesched
            ${CORTLET_SYSTEM_LIBS}
    )

endif()

# ---------------------------------------------------------
# Install rules
# ---------------------------------------------------------

include(GNUInstallDirs)

install(
        FILES
        "${CMAKE_SOURCE_DIR}/include/cortlet-upgradesched.h"

        DESTINATION
        "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
        FILES
        "${RUST_STATIC_LIBRARY}"

        DESTINATION
        "${CMAKE_INSTALL_LIBDIR}"
)

# ---------------------------------------------------------
# Status output
# ---------------------------------------------------------

message(STATUS "")
message(STATUS "libcortlet-upgradesched v${PROJECT_VERSION}")
message(STATUS "----------------------------------------")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "Cargo mode:        ${CARGO_BUILD_MODE}")
message(STATUS "Tests:             ${CORTLET_BUILD_TESTS}")
message(STATUS "Benchmarks:        ${CORTLET_BUILD_BENCHMARKS}")
message(STATUS "Rust target dir:   ${CARGO_TARGET_DIR}")
message(STATUS "")
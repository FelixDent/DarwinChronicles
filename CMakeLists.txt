cmake_minimum_required(VERSION 3.24)
project(DarwinChronicles
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Evolutionary ecosystem simulation"
)

# ── Prevent in-source builds ──────────────────────────────────────────────────
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -B build")
endif()

# ── C++20 ─────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Export compile_commands.json for tooling ──────────────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ───────────────────────────────────────────────────────────────────
option(DARWIN_BUILD_TESTS    "Build unit tests"          ON)
option(DARWIN_BUILD_BENCHES  "Build benchmarks"          OFF)
option(DARWIN_BUILD_EXAMPLES  "Build example programs"    ON)
option(DARWIN_BUILD_SANDBOXES "Build sandbox programs"   ON)

# ── Warning flags (INTERFACE target) ──────────────────────────────────────────
add_library(darwin_warnings INTERFACE)
target_compile_options(darwin_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wnull-dereference
        -Wformat=2 -Wimplicit-fallthrough>
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /permissive->
)

# ── Dependencies ──────────────────────────────────────────────────────────────
include(cmake/Dependencies.cmake)

# ── Source modules ────────────────────────────────────────────────────────────
add_subdirectory(src/environment)
add_subdirectory(src/world)
add_subdirectory(src/organisms)
add_subdirectory(src/evolution)
add_subdirectory(src/simulation)
add_subdirectory(src/rendering)

# ── Main executable ──────────────────────────────────────────────────────────
add_executable(darwin src/main.cpp)
target_link_libraries(darwin PRIVATE
    darwin::simulation
    darwin::rendering
    darwin_warnings
)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(DARWIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Benchmarks ────────────────────────────────────────────────────────────────
if(DARWIN_BUILD_BENCHES)
    add_subdirectory(benches)
endif()

# ── Examples ──────────────────────────────────────────────────────────────────
if(DARWIN_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Sandboxes ────────────────────────────────────────────────────────────────
if(DARWIN_BUILD_SANDBOXES)
    add_subdirectory(sandboxes)
endif()

include(FetchContent)

# ── EnTT (ECS framework) ─────────────────────────────────────────────────────
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
    GIT_SHALLOW    TRUE
)

# ── FastNoiseLite (procedural noise) ─────────────────────────────────────────
FetchContent_Declare(
    FastNoiseLite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG        v1.1.1
    GIT_SHALLOW    TRUE
)

# ── SDL2 (rendering) ─────────────────────────────────────────────────────────
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    message(STATUS "SDL2 not found on system, fetching via FetchContent...")
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.10
        GIT_SHALLOW    TRUE
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TEST   OFF CACHE BOOL "" FORCE)
endif()

# ── Catch2 (testing) ─────────────────────────────────────────────────────────
if(DARWIN_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
    )
endif()

# ── Google Benchmark ──────────────────────────────────────────────────────────
if(DARWIN_BUILD_BENCHES)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
        GIT_SHALLOW    TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
endif()

# ── Make available ────────────────────────────────────────────────────────────
set(_deps EnTT FastNoiseLite)

if(NOT SDL2_FOUND)
    list(APPEND _deps SDL2)
endif()

if(DARWIN_BUILD_TESTS)
    list(APPEND _deps Catch2)
endif()

if(DARWIN_BUILD_BENCHES)
    list(APPEND _deps benchmark)
endif()

FetchContent_MakeAvailable(${_deps})

# FastNoiseLite is header-only; create an INTERFACE target if it doesn't exist
if(NOT TARGET FastNoiseLite)
    add_library(FastNoiseLite INTERFACE)
    target_include_directories(FastNoiseLite INTERFACE
        ${fastnoiselite_SOURCE_DIR}/Cpp
    )
endif()

# Ensure SDL2::SDL2 imported target exists for older system SDL2 or FetchContent builds
if(NOT TARGET SDL2::SDL2)
    if(TARGET SDL2-static)
        add_library(SDL2::SDL2 ALIAS SDL2-static)
    elseif(TARGET SDL2)
        # SDL2 target exists but without namespace (FetchContent)
        add_library(SDL2::SDL2 ALIAS SDL2)
    elseif(SDL2_FOUND AND SDL2_LIBRARIES)
        # Older system SDL2 without modern CMake targets
        add_library(SDL2::SDL2 IMPORTED INTERFACE)
        set_target_properties(SDL2::SDL2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}"
        )
    endif()
endif()

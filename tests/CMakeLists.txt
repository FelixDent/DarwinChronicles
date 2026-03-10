include(Catch2::Catch2WithMain OPTIONAL)

add_executable(darwin_tests
    test_environment.cpp
    test_world.cpp
    test_organisms.cpp
    test_evolution.cpp
    test_simulation.cpp
)

target_link_libraries(darwin_tests PRIVATE
    Catch2::Catch2WithMain
    darwin::simulation
    darwin::evolution
    darwin::world
    darwin::organisms
    darwin::environment
    darwin_warnings
)

# ── Autotile tests (standalone, no darwin::* modules, no SDL) ────────────
find_package(Threads REQUIRED)
add_executable(autotile_tests
    test_autotile.cpp
    ${CMAKE_SOURCE_DIR}/sandboxes/weather/terrain_gen.cpp
    ${CMAKE_SOURCE_DIR}/sandboxes/weather/tile_texture.cpp
)
target_include_directories(autotile_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/sandboxes/weather
)
target_link_libraries(autotile_tests PRIVATE
    Catch2::Catch2WithMain
    FastNoiseLite
    darwin_warnings
    Threads::Threads
)

include(CTest)
include(Catch)
catch_discover_tests(darwin_tests)
catch_discover_tests(autotile_tests)

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

include(CTest)
include(Catch)
catch_discover_tests(darwin_tests)

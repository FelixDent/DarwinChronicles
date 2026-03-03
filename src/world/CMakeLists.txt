add_library(darwin_world STATIC
    grid.cpp
    generation.cpp
    biomes.cpp
    fields.cpp
    regeneration.cpp
)
add_library(darwin::world ALIAS darwin_world)

target_include_directories(darwin_world PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(darwin_world
    PUBLIC  darwin::environment EnTT::EnTT
    PRIVATE FastNoiseLite darwin_warnings
)

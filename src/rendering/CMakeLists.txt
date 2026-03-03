add_library(darwin_rendering STATIC
    sprite_gen.cpp
    camera.cpp
    debug_viz.cpp
    ui.cpp
)
add_library(darwin::rendering ALIAS darwin_rendering)

target_include_directories(darwin_rendering PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(darwin_rendering
    PUBLIC  darwin::simulation darwin::world darwin::organisms EnTT::EnTT
    PRIVATE SDL2::SDL2 darwin_warnings
)

set(target sandbox_worldgen)
add_executable(${target}
    main.cpp
    config.cpp
    renderer.cpp
    telemetry.cpp
    # Shared from weather sandbox (canonical terrain generation + tile textures)
    ${CMAKE_SOURCE_DIR}/sandboxes/weather/terrain_gen.cpp
    ${CMAKE_SOURCE_DIR}/sandboxes/weather/tile_texture.cpp
)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/sandboxes/weather
)
target_link_libraries(${target} PRIVATE
    SDL2::SDL2
    FastNoiseLite
    darwin_warnings
    Threads::Threads
)
target_compile_definitions(${target} PRIVATE
    ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
)

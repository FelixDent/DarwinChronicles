set(target sandbox_spritetest)
add_executable(${target}
    main.cpp
    ${CMAKE_SOURCE_DIR}/sandboxes/veggen/plant_sprite.cpp
    ${CMAKE_SOURCE_DIR}/sandboxes/veggen/vegetation.cpp
    ${CMAKE_SOURCE_DIR}/sandboxes/veggen/metrics.cpp
)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/sandboxes/veggen
)
target_link_libraries(${target} PRIVATE SDL2::SDL2 darwin_warnings)

# Helper function for sandbox executables.
# Sandboxes are standalone — they do NOT link darwin::* modules.
function(add_darwin_sandbox name)
    set(target sandbox_${name})
    file(GLOB sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
    add_executable(${target} ${sources})
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${target} PRIVATE
        SDL2::SDL2
        FastNoiseLite
        darwin_warnings
    )
    target_compile_definitions(${target} PRIVATE
        ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
    )
endfunction()

add_subdirectory(worldgen)

function(add_darwin_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE
        darwin::simulation
        darwin::rendering
        darwin_warnings
    )
endfunction()

add_darwin_example(earth_like)
add_darwin_example(mars_like)

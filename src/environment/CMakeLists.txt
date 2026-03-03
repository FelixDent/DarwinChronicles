add_library(darwin_environment STATIC
    planetary.cpp
    climate.cpp
    params.cpp
)
add_library(darwin::environment ALIAS darwin_environment)

target_include_directories(darwin_environment PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(darwin_environment
    PUBLIC  EnTT::EnTT
    PRIVATE darwin_warnings
)

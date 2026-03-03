add_library(darwin_organisms STATIC
    genome.cpp
    brain.cpp
    metabolism.cpp
    sensors.cpp
    components.cpp
)
add_library(darwin::organisms ALIAS darwin_organisms)

target_include_directories(darwin_organisms PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(darwin_organisms
    PUBLIC  EnTT::EnTT
    PRIVATE darwin_warnings
)

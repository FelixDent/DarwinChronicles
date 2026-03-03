add_library(darwin_simulation STATIC
    tick.cpp
    organism_update.cpp
    scheduling.cpp
)
add_library(darwin::simulation ALIAS darwin_simulation)

target_include_directories(darwin_simulation PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(darwin_simulation
    PUBLIC  darwin::environment darwin::world darwin::organisms darwin::evolution EnTT::EnTT
    PRIVATE darwin_warnings
)

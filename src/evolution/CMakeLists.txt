add_library(darwin_evolution STATIC
    reproduction.cpp
    mutations.cpp
    selection.cpp
)
add_library(darwin::evolution ALIAS darwin_evolution)

target_include_directories(darwin_evolution PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(darwin_evolution
    PUBLIC  darwin::organisms EnTT::EnTT
    PRIVATE darwin_warnings
)

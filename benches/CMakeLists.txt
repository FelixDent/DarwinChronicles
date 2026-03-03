add_executable(darwin_benchmarks
    bench_simulation.cpp
)

target_link_libraries(darwin_benchmarks PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    darwin::simulation
    darwin_warnings
)

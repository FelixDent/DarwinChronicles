#include <benchmark/benchmark.h>
#include <darwin/simulation/tick.h>

static void BM_SimulationTick(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(0);
    }
}
BENCHMARK(BM_SimulationTick);

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "vegetation.h"

namespace veggen {

// ── Death cause tracking ────────────────────────────────────────────────────

enum class DeathCause : uint8_t {
    None = 0,
    Drought,
    Cold,
    Heat,
    Competition,
    Senescence,  // age-related
};

const char* death_cause_name(DeathCause c);

// ── Age bins (based on species growth speed) ────────────────────────────────

enum class AgeBin : uint8_t { Seedling = 0, Juvenile, Mature, Old, COUNT };
constexpr int NUM_AGE_BINS = static_cast<int>(AgeBin::COUNT);

const char* age_bin_name(AgeBin b);

// Map a plant's age to a bin based on its growth speed (faster growers mature sooner)
AgeBin classify_age(float age_days, float growth_speed);

// ── Per-snapshot metrics ────────────────────────────────────────────────────

struct PerSpeciesMetrics {
    int population = 0;
    int age_hist[NUM_AGE_BINS] = {};
    float avg_biomass = 0;
    float avg_stress = 0;
    float avg_growth_rate = 0;
    int births = 0;   // new plants this interval
    int deaths = 0;   // deaths this interval
};

struct MetricsSnapshot {
    float day = 0;

    // Per-archetype breakdown
    PerSpeciesMetrics by_species[NUM_ARCHETYPES];

    // Aggregate population
    int total_living = 0;
    int total_dead_pending = 0;  // dead but not yet removed

    // Mortality breakdown (this interval)
    int deaths_drought = 0;
    int deaths_cold = 0;
    int deaths_heat = 0;
    int deaths_competition = 0;
    int deaths_senescence = 0;
    int deaths_total = 0;

    // Recruitment
    int new_plants = 0;
    int removed_plants = 0;

    // Spatial
    float plants_per_tile_mean = 0;
    float plants_per_tile_variance = 0;

    // Environment
    float avg_temperature = 0;
    float avg_moisture = 0;

    // Size distribution
    float biomass_mean = 0;
    float biomass_variance = 0;

    // Growth vs density correlation (Pearson r)
    float growth_density_corr = 0;

    // Species fractions (of living plants)
    float species_fraction[NUM_ARCHETYPES] = {};

    // Effective replacement ratio (births / mature plants)
    float replacement_ratio = 0;
};

// ── Metrics collector ───────────────────────────────────────────────────────

class MetricsCollector {
public:
    void reset();

    // Called each sim update with dt_days
    void on_update(const SimState& sim, float dt_days);

    // Called when a plant dies (before removal)
    void on_death(PlantArchetype arch, DeathCause cause);

    // Called when a new plant spawns
    void on_birth(PlantArchetype arch);

    // Called when a dead plant is removed from the vector
    void on_removal();

    // Get all snapshots collected so far
    const std::vector<MetricsSnapshot>& snapshots() const { return snapshots_; }

    // Export all snapshots to CSV file
    void export_csv(const char* filename) const;

    // Print summary to stdout
    void print_summary(const char* biome_name) const;

    // Configurable snapshot interval (sim days)
    float snapshot_interval = 30.0f;

private:
    std::vector<MetricsSnapshot> snapshots_;
    float accumulator_ = 0;

    // Per-interval counters (reset each snapshot)
    int interval_births_ = 0;
    int interval_deaths_ = 0;
    int interval_removals_ = 0;
    int interval_deaths_by_cause_[6] = {};  // indexed by DeathCause

    void take_snapshot(const SimState& sim);
};

}  // namespace veggen

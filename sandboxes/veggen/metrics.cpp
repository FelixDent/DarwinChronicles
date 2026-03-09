#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace veggen {

// ── Helpers ─────────────────────────────────────────────────────────────────

const char* death_cause_name(DeathCause c) {
    switch (c) {
        case DeathCause::None:
            return "None";
        case DeathCause::Drought:
            return "Drought";
        case DeathCause::Cold:
            return "Cold";
        case DeathCause::Heat:
            return "Heat";
        case DeathCause::Competition:
            return "Competition";
        case DeathCause::Senescence:
            return "Senescence";
    }
    return "Unknown";
}

const char* age_bin_name(AgeBin b) {
    switch (b) {
        case AgeBin::Seedling:
            return "Seedling";
        case AgeBin::Juvenile:
            return "Juvenile";
        case AgeBin::Mature:
            return "Mature";
        case AgeBin::Old:
            return "Old";
        default:
            return "Unknown";
    }
}

AgeBin classify_age(float age_days, float growth_speed) {
    // Time to maturity: ~1/growth_speed days (when biomass reaches ~1.0 at ideal conditions)
    // Seedling: 0..10% of maturity
    // Juvenile: 10..50%
    // Mature: 50..200%
    // Old: >200%
    float maturity_days = 1.0f / std::max(growth_speed, 0.0001f);
    float age_frac = age_days / maturity_days;
    if (age_frac < 0.10f)
        return AgeBin::Seedling;
    if (age_frac < 0.50f)
        return AgeBin::Juvenile;
    if (age_frac < 2.0f)
        return AgeBin::Mature;
    return AgeBin::Old;
}

// ── MetricsCollector ────────────────────────────────────────────────────────

void MetricsCollector::reset() {
    snapshots_.clear();
    accumulator_ = 0;
    interval_births_ = 0;
    interval_deaths_ = 0;
    interval_removals_ = 0;
    for (auto& d : interval_deaths_by_cause_)
        d = 0;
}

void MetricsCollector::on_death(PlantArchetype arch, DeathCause cause) {
    ++interval_deaths_;
    int ci = static_cast<int>(cause);
    if (ci >= 0 && ci < 6)
        ++interval_deaths_by_cause_[ci];
    (void)arch;  // could break down by species if needed
}

void MetricsCollector::on_birth(PlantArchetype arch) {
    ++interval_births_;
    (void)arch;
}

void MetricsCollector::on_removal() {
    ++interval_removals_;
}

void MetricsCollector::on_update(const SimState& sim, float dt_days) {
    accumulator_ += dt_days;
    if (accumulator_ >= snapshot_interval) {
        accumulator_ -= snapshot_interval;
        take_snapshot(sim);
    }
}

void MetricsCollector::take_snapshot(const SimState& sim) {
    MetricsSnapshot snap;
    snap.day = sim.elapsed_days;

    // Count living and dead plants
    int living = 0;
    int dead_pending = 0;
    for (const auto& p : sim.plants) {
        if (p.health == PlantHealth::Dead)
            ++dead_pending;
        else
            ++living;
    }
    snap.total_living = living;
    snap.total_dead_pending = dead_pending;

    // Per-species metrics
    float total_growth_sum = 0;
    float total_biomass_sum = 0;
    float total_biomass_sq = 0;

    // Compute growth rate and neighbor count per living plant for correlation
    struct PlantSample {
        float growth_rate;
        float neighbor_count;
    };
    std::vector<PlantSample> samples;
    samples.reserve(static_cast<size_t>(living));

    for (const auto& p : sim.plants) {
        if (p.health == PlantHealth::Dead)
            continue;

        int ai = static_cast<int>(p.traits.archetype);
        auto& sp = snap.by_species[ai];
        sp.population++;

        AgeBin bin = classify_age(p.age_days, p.traits.growth_speed);
        sp.age_hist[static_cast<int>(bin)]++;

        sp.avg_biomass += p.biomass;
        sp.avg_stress += p.stress;

        total_biomass_sum += p.biomass;
        total_biomass_sq += p.biomass * p.biomass;

        // Approximate growth rate from suitability and water
        const auto& tile = sim.tiles[p.tile_y][p.tile_x];
        float suit = 0;
        if (tile.temperature >= p.traits.temp_min_c && tile.temperature <= p.traits.temp_max_c) {
            float dist_to_opt = std::abs(tile.temperature - p.traits.temp_opt_c);
            float range = std::max(p.traits.temp_max_c - p.traits.temp_min_c, 1.0f);
            suit = 1.0f - (dist_to_opt / range);
            suit = std::clamp(suit, 0.0f, 1.0f);
        }
        float water_factor =
            std::clamp(p.water_reserve / std::max(p.traits.water_need, 0.1f), 0.0f, 1.0f);
        float growth_rate = p.traits.growth_speed * suit * water_factor * (1.0f - p.stress);
        sp.avg_growth_rate += growth_rate;
        total_growth_sum += growth_rate;

        // Count neighbors on same tile
        float neighbors = 0;
        for (const auto& other : sim.plants) {
            if (&other == &p || other.health == PlantHealth::Dead)
                continue;
            if (other.tile_x == p.tile_x && other.tile_y == p.tile_y)
                neighbors += 1.0f;
        }
        samples.push_back({growth_rate, neighbors});
    }

    // Finalize per-species averages
    for (int i = 0; i < NUM_ARCHETYPES; ++i) {
        auto& sp = snap.by_species[i];
        if (sp.population > 0) {
            float n = static_cast<float>(sp.population);
            sp.avg_biomass /= n;
            sp.avg_stress /= n;
            sp.avg_growth_rate /= n;
        }
        snap.species_fraction[i] =
            (living > 0) ? static_cast<float>(sp.population) / static_cast<float>(living) : 0;
    }

    // Aggregate biomass stats
    if (living > 0) {
        snap.biomass_mean = total_biomass_sum / static_cast<float>(living);
        snap.biomass_variance =
            total_biomass_sq / static_cast<float>(living) - snap.biomass_mean * snap.biomass_mean;
        snap.biomass_variance = std::max(snap.biomass_variance, 0.0f);
    }

    // Spatial: plants per tile
    int tile_counts[GRID_SIZE][GRID_SIZE] = {};
    for (const auto& p : sim.plants) {
        if (p.health != PlantHealth::Dead)
            tile_counts[p.tile_y][p.tile_x]++;
    }
    float tile_sum = 0, tile_sq = 0;
    int n_tiles = GRID_SIZE * GRID_SIZE;
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            float c = static_cast<float>(tile_counts[y][x]);
            tile_sum += c;
            tile_sq += c * c;
        }
    }
    snap.plants_per_tile_mean = tile_sum / static_cast<float>(n_tiles);
    snap.plants_per_tile_variance = tile_sq / static_cast<float>(n_tiles) -
                                    snap.plants_per_tile_mean * snap.plants_per_tile_mean;
    snap.plants_per_tile_variance = std::max(snap.plants_per_tile_variance, 0.0f);

    // Environment averages
    float temp_sum = 0, moist_sum = 0;
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            temp_sum += sim.tiles[y][x].temperature;
            moist_sum += sim.tiles[y][x].soil_moisture();
        }
    }
    snap.avg_temperature = temp_sum / static_cast<float>(n_tiles);
    snap.avg_moisture = moist_sum / static_cast<float>(n_tiles);

    // Growth vs density correlation (Pearson r)
    if (samples.size() >= 3) {
        float n = static_cast<float>(samples.size());
        float sum_g = 0, sum_d = 0, sum_gd = 0, sum_g2 = 0, sum_d2 = 0;
        for (const auto& s : samples) {
            sum_g += s.growth_rate;
            sum_d += s.neighbor_count;
            sum_gd += s.growth_rate * s.neighbor_count;
            sum_g2 += s.growth_rate * s.growth_rate;
            sum_d2 += s.neighbor_count * s.neighbor_count;
        }
        float num = n * sum_gd - sum_g * sum_d;
        float den = std::sqrt((n * sum_g2 - sum_g * sum_g) * (n * sum_d2 - sum_d * sum_d));
        snap.growth_density_corr = (den > 1e-9f) ? num / den : 0.0f;
    }

    // Interval counters
    snap.new_plants = interval_births_;
    snap.deaths_total = interval_deaths_;
    snap.removed_plants = interval_removals_;
    snap.deaths_drought = interval_deaths_by_cause_[static_cast<int>(DeathCause::Drought)];
    snap.deaths_cold = interval_deaths_by_cause_[static_cast<int>(DeathCause::Cold)];
    snap.deaths_heat = interval_deaths_by_cause_[static_cast<int>(DeathCause::Heat)];
    snap.deaths_competition = interval_deaths_by_cause_[static_cast<int>(DeathCause::Competition)];
    snap.deaths_senescence = interval_deaths_by_cause_[static_cast<int>(DeathCause::Senescence)];

    // Replacement ratio: births / mature plants
    int mature_count = 0;
    for (const auto& p : sim.plants) {
        if (p.health != PlantHealth::Dead && p.phase == GrowthPhase::Large)
            ++mature_count;
    }
    snap.replacement_ratio =
        (mature_count > 0) ? static_cast<float>(interval_births_) / static_cast<float>(mature_count)
                           : 0.0f;

    // Reset interval counters
    interval_births_ = 0;
    interval_deaths_ = 0;
    interval_removals_ = 0;
    for (auto& d : interval_deaths_by_cause_)
        d = 0;

    // Per-species births/deaths — set from interval (simplified: total only for now)
    snap.by_species[0].births = snap.new_plants;  // placeholder — needs per-species tracking

    snapshots_.push_back(snap);
}

void MetricsCollector::export_csv(const char* filename) const {
    FILE* f = std::fopen(filename, "w");
    if (!f) {
        std::fprintf(stderr, "Failed to open %s for writing\n", filename);
        return;
    }

    // Header
    std::fprintf(f,
                 "day,living,dead_pending,new_plants,deaths_total,"
                 "deaths_drought,deaths_cold,deaths_heat,deaths_competition,deaths_senescence,"
                 "biomass_mean,biomass_var,"
                 "plants_tile_mean,plants_tile_var,"
                 "avg_temp,avg_moisture,"
                 "growth_density_corr,replacement_ratio");

    // Per-archetype population columns
    const char* arch_names[] = {"grass",     "shrub", "conifer", "broadleaf",
                                "succulent", "moss",  "palm",    "tropical"};
    for (int i = 0; i < NUM_ARCHETYPES; ++i)
        std::fprintf(f, ",pop_%s", arch_names[i]);
    for (int i = 0; i < NUM_ARCHETYPES; ++i)
        std::fprintf(f, ",frac_%s", arch_names[i]);
    // Per-archetype avg stress
    for (int i = 0; i < NUM_ARCHETYPES; ++i)
        std::fprintf(f, ",stress_%s", arch_names[i]);
    // Age distribution (aggregated across species)
    std::fprintf(f, ",age_seedling,age_juvenile,age_mature,age_old");
    std::fprintf(f, "\n");

    // Data rows
    for (const auto& s : snapshots_) {
        std::fprintf(
            f,
            "%.1f,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
            "%.4f,%.4f,%.2f,%.2f,%.1f,%.3f,%.3f,%.3f",
            static_cast<double>(s.day), s.total_living, s.total_dead_pending, s.new_plants,
            s.deaths_total, s.deaths_drought, s.deaths_cold, s.deaths_heat, s.deaths_competition,
            s.deaths_senescence, static_cast<double>(s.biomass_mean),
            static_cast<double>(s.biomass_variance), static_cast<double>(s.plants_per_tile_mean),
            static_cast<double>(s.plants_per_tile_variance), static_cast<double>(s.avg_temperature),
            static_cast<double>(s.avg_moisture), static_cast<double>(s.growth_density_corr),
            static_cast<double>(s.replacement_ratio));

        for (int i = 0; i < NUM_ARCHETYPES; ++i)
            std::fprintf(f, ",%d", s.by_species[i].population);
        for (int i = 0; i < NUM_ARCHETYPES; ++i)
            std::fprintf(f, ",%.3f", static_cast<double>(s.species_fraction[i]));
        for (int i = 0; i < NUM_ARCHETYPES; ++i)
            std::fprintf(f, ",%.3f", static_cast<double>(s.by_species[i].avg_stress));

        // Aggregate age histogram across all species
        int age_totals[NUM_AGE_BINS] = {};
        for (int i = 0; i < NUM_ARCHETYPES; ++i)
            for (int b = 0; b < NUM_AGE_BINS; ++b)
                age_totals[b] += s.by_species[i].age_hist[b];
        for (int b = 0; b < NUM_AGE_BINS; ++b)
            std::fprintf(f, ",%d", age_totals[b]);

        std::fprintf(f, "\n");
    }

    std::fclose(f);
    std::printf("Exported %zu snapshots to %s\n", snapshots_.size(), filename);
}

void MetricsCollector::print_summary(const char* biome_name) const {
    if (snapshots_.empty()) {
        std::printf("[%s] No metrics collected\n", biome_name);
        return;
    }

    const auto& first = snapshots_.front();
    const auto& last = snapshots_.back();

    std::printf("\n=== %s (day %.0f - %.0f, %zu snapshots) ===\n", biome_name,
                static_cast<double>(first.day), static_cast<double>(last.day), snapshots_.size());

    // Population trajectory
    std::printf("  Population: %d -> %d (living)\n", first.total_living, last.total_living);

    // Species composition at end
    std::printf("  Species composition (final):\n");
    const char* arch_names[] = {"Grass",     "Shrub", "Conifer", "Broadleaf",
                                "Succulent", "Moss",  "Palm",    "Tropical"};
    for (int i = 0; i < NUM_ARCHETYPES; ++i) {
        if (last.by_species[i].population > 0) {
            std::printf("    %-10s %3d (%.0f%%)\n", arch_names[i], last.by_species[i].population,
                        static_cast<double>(last.species_fraction[i] * 100.0f));
        }
    }

    // Mortality summary (sum across all snapshots)
    int total_d = 0, total_drought = 0, total_cold = 0, total_heat = 0;
    int total_comp = 0, total_senes = 0, total_births = 0;
    for (const auto& s : snapshots_) {
        total_d += s.deaths_total;
        total_drought += s.deaths_drought;
        total_cold += s.deaths_cold;
        total_heat += s.deaths_heat;
        total_comp += s.deaths_competition;
        total_senes += s.deaths_senescence;
        total_births += s.new_plants;
    }
    std::printf("  Total births: %d, deaths: %d\n", total_births, total_d);
    if (total_d > 0) {
        std::printf("  Death causes: drought=%d cold=%d heat=%d competition=%d senescence=%d\n",
                    total_drought, total_cold, total_heat, total_comp, total_senes);
    }

    // Age distribution at end
    int age_totals[NUM_AGE_BINS] = {};
    for (int i = 0; i < NUM_ARCHETYPES; ++i)
        for (int b = 0; b < NUM_AGE_BINS; ++b)
            age_totals[b] += last.by_species[i].age_hist[b];
    std::printf("  Age dist: seedling=%d juvenile=%d mature=%d old=%d\n", age_totals[0],
                age_totals[1], age_totals[2], age_totals[3]);

    // Growth-density correlation (average across snapshots)
    float corr_sum = 0;
    int corr_count = 0;
    for (const auto& s : snapshots_) {
        if (s.total_living >= 3) {
            corr_sum += s.growth_density_corr;
            ++corr_count;
        }
    }
    if (corr_count > 0) {
        std::printf("  Growth-density correlation: %.3f (expected: negative)\n",
                    static_cast<double>(corr_sum / static_cast<float>(corr_count)));
    }

    // Population stability: coefficient of variation over last half of run
    if (snapshots_.size() >= 10) {
        size_t half = snapshots_.size() / 2;
        float sum = 0, sq = 0;
        int cnt = 0;
        for (size_t i = half; i < snapshots_.size(); ++i) {
            float v = static_cast<float>(snapshots_[i].total_living);
            sum += v;
            sq += v * v;
            ++cnt;
        }
        float mean = sum / static_cast<float>(cnt);
        float var = sq / static_cast<float>(cnt) - mean * mean;
        float cv = (mean > 0) ? std::sqrt(std::max(var, 0.0f)) / mean : 0;
        std::printf("  Population stability (CV): %.3f (lower=more stable)\n",
                    static_cast<double>(cv));
    }

    // Replacement ratio average
    float rr_sum = 0;
    int rr_count = 0;
    for (const auto& s : snapshots_) {
        if (s.replacement_ratio > 0) {
            rr_sum += s.replacement_ratio;
            ++rr_count;
        }
    }
    if (rr_count > 0) {
        std::printf("  Avg replacement ratio: %.2f (target: ~1.0)\n",
                    static_cast<double>(rr_sum / static_cast<float>(rr_count)));
    }

    std::printf("\n");
}

}  // namespace veggen

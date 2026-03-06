#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>

#include "plant_sprite.h"
#include "vegetation.h"

static constexpr int PADDING = 4;
static constexpr uint32_t BG_COLOR = 0xFF1A1A2E;

// ── Species profiles ────────────────────────────────────────────────────────
// Each species is a coherent set of traits adapted to a specific biome.
// Different species of the same archetype should look visually distinct because
// their ecological properties (drought tolerance, water need, temperature range)
// drive canopy density, trunk thickness, branch spread, color, etc.

struct SpeciesProfile {
    const char* name;
    veggen::PlantArchetype archetype;
    // Ecological traits that drive visual output
    float water_need;
    float drought_tol;
    float temp_min_c;
    float temp_opt_c;
    float temp_max_c;
    uint8_t base_hue;     // palette position
    uint8_t leafiness;    // 0=sparse, 255=lush
    uint8_t branchiness;  // 0=simple, 255=complex
};

// clang-format off
static constexpr int NUM_SPECIES = 19;
static const SpeciesProfile SPECIES[NUM_SPECIES] = {
    // Broadleaf trees — varied biomes
    {"Tropical Canopy",     veggen::PlantArchetype::BroadleafTree,
     0.8f, 0.1f, 10.0f, 28.0f, 40.0f,  80, 250, 220},
    {"Temperate Oak",       veggen::PlantArchetype::BroadleafTree,
     0.5f, 0.3f,  -5.0f, 18.0f, 32.0f, 140, 160, 180},
    {"Dry Savanna Acacia",  veggen::PlantArchetype::BroadleafTree,
     0.15f, 0.85f,  5.0f, 32.0f, 48.0f, 210, 60,  140},
    {"Flowering Jacaranda", veggen::PlantArchetype::BroadleafTree,
     0.6f, 0.2f,   5.0f, 24.0f, 36.0f,   0, 180, 200},

    // Tropical / rainforest canopy trees
    {"Emergent Kapok",      veggen::PlantArchetype::TropicalTree,
     0.9f, 0.05f, 12.0f, 28.0f, 38.0f,  70, 250, 200},
    {"Mahogany",            veggen::PlantArchetype::TropicalTree,
     0.7f, 0.15f, 10.0f, 26.0f, 36.0f, 100, 200, 240},
    {"Strangler Fig",       veggen::PlantArchetype::TropicalTree,
     0.8f, 0.1f,  8.0f, 27.0f, 40.0f,  60, 240, 180},

    // Conifers — cold/mountain adapted
    {"Boreal Spruce",       veggen::PlantArchetype::Conifer,
     0.4f, 0.2f, -30.0f, 8.0f,  22.0f,  60, 220, 230},
    {"Mountain Pine",       veggen::PlantArchetype::Conifer,
     0.3f, 0.5f, -20.0f, 12.0f, 28.0f, 100, 130, 150},
    {"Coastal Redwood",     veggen::PlantArchetype::Conifer,
     0.7f, 0.1f,   0.0f, 16.0f, 30.0f,  40, 240, 200},

    // Palms — tropical
    {"Coconut Palm",        veggen::PlantArchetype::Palm,
     0.6f, 0.2f,  12.0f, 28.0f, 42.0f,  90, 200, 120},
    {"Date Palm",           veggen::PlantArchetype::Palm,
     0.2f, 0.7f,   5.0f, 35.0f, 50.0f, 160, 140,  80},
    {"Fan Palm",            veggen::PlantArchetype::Palm,
     0.7f, 0.1f,  10.0f, 26.0f, 38.0f,  70, 240, 160},

    // Shrubs — wide biome range
    {"Jungle Undergrowth",  veggen::PlantArchetype::Shrub,
     0.7f, 0.1f,   8.0f, 25.0f, 38.0f,  70, 240, 180},
    {"Desert Scrub",        veggen::PlantArchetype::Shrub,
     0.1f, 0.9f,   0.0f, 32.0f, 48.0f, 220, 40,  80},

    // Ground cover
    {"Tropical Grass",      veggen::PlantArchetype::Grass,
     0.6f, 0.2f,   5.0f, 26.0f, 40.0f,  60, 200, 120},
    {"Savanna Grass",       veggen::PlantArchetype::Grass,
     0.2f, 0.6f,   0.0f, 30.0f, 45.0f, 190, 100, 100},
    {"Desert Succulent",    veggen::PlantArchetype::Succulent,
     0.05f, 0.95f,  5.0f, 35.0f, 50.0f, 160, 180, 60},
    {"Arctic Moss",         veggen::PlantArchetype::Moss,
     0.5f, 0.1f, -30.0f, 4.0f,  15.0f,  50, 160, 40},
};
// clang-format on

static veggen::PlantTraits make_traits_from_profile(const SpeciesProfile& sp, uint32_t seed,
                                                     std::mt19937& rng) {
    // Small per-instance jitter so same species looks similar but not identical
    std::uniform_int_distribution<int> jitter(-8, 8);
    std::uniform_real_distribution<float> small_var(-0.03f, 0.03f);

    veggen::PlantTraits t{};
    t.archetype = sp.archetype;
    t.water_need = std::clamp(sp.water_need + small_var(rng), 0.0f, 1.0f);
    t.drought_tol = std::clamp(sp.drought_tol + small_var(rng), 0.0f, 1.0f);
    t.temp_min_c = sp.temp_min_c + small_var(rng) * 30.0f;
    t.temp_opt_c = sp.temp_opt_c + small_var(rng) * 10.0f;
    t.temp_max_c = sp.temp_max_c + small_var(rng) * 10.0f;
    t.growth_speed = 0.02f;
    t.fertility_need = 0.3f;
    t.cluster_affinity = 0.3f;
    t.sprite_seed = seed;
    t.base_hue = static_cast<uint8_t>(
        std::clamp(static_cast<int>(sp.base_hue) + jitter(rng), 0, 255));
    t.leafiness = static_cast<uint8_t>(
        std::clamp(static_cast<int>(sp.leafiness) + jitter(rng), 0, 255));
    t.branchiness = static_cast<uint8_t>(
        std::clamp(static_cast<int>(sp.branchiness) + jitter(rng), 0, 255));
    return t;
}

// ── Rendering ───────────────────────────────────────────────────────────────

static SDL_Surface* upscale(const veggen::Sprite& sprite, int scale) {
    int sw = sprite.w * scale;
    int sh = sprite.h * scale;
    SDL_Surface* surf =
        SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_RGBA32);
    auto* dst = static_cast<uint32_t*>(surf->pixels);
    for (int y = 0; y < sh; ++y) {
        int sy = y / scale;
        for (int x = 0; x < sw; ++x) {
            int sx = x / scale;
            dst[y * (surf->pitch / 4) + x] =
                sprite.pixels[static_cast<size_t>(sy * sprite.w + sx)];
        }
    }
    return surf;
}

// Columns: 3 instances of L-Healthy (same species, different seed) + L-Stressed + L-Dead
static constexpr int COLS = 5;
struct ColSpec {
    veggen::GrowthPhase phase;
    veggen::PlantHealth health;
    const char* label;
};
static const ColSpec col_specs[COLS] = {
    {veggen::GrowthPhase::Large, veggen::PlantHealth::Healthy, "L-Inst1"},
    {veggen::GrowthPhase::Large, veggen::PlantHealth::Healthy, "L-Inst2"},
    {veggen::GrowthPhase::Large, veggen::PlantHealth::Healthy, "L-Inst3"},
    {veggen::GrowthPhase::Large, veggen::PlantHealth::Stressed, "L-Stressed"},
    {veggen::GrowthPhase::Large, veggen::PlantHealth::Dead, "L-Dead"},
};

int main(int argc, char* argv[]) {
    if (SDL_Init(0) < 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    uint32_t master_seed;
    if (argc > 1) {
        master_seed = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    } else {
        master_seed = static_cast<uint32_t>(std::time(nullptr));
    }

    // Optional filter: "all" (default), archetype name, or species index
    int filter_species = -1;  // -1 = all species
    if (argc > 2 && std::strcmp(argv[2], "all") != 0) {
        filter_species = static_cast<int>(std::strtol(argv[2], nullptr, 10));
        if (filter_species < 0 || filter_species >= NUM_SPECIES) {
            std::fprintf(stderr, "Species index must be 0..%d\n", NUM_SPECIES - 1);
            return 1;
        }
    }

    int num_rows = (filter_species >= 0) ? 1 : NUM_SPECIES;
    int scale = (filter_species >= 0) ? 14 : 8;

    std::printf("Master seed: %u\n", master_seed);
    std::mt19937 rng(master_seed);

    // Generate sprites: each row = species, cols = instances + health variants
    veggen::Sprite sprites[NUM_SPECIES][COLS];
    int max_w = 0, max_h = 0;

    for (int si = 0; si < NUM_SPECIES; ++si) {
        if (filter_species >= 0 && si != filter_species)
            continue;

        const auto& sp = SPECIES[si];
        for (int c = 0; c < COLS; ++c) {
            // Each column gets a different instance seed for within-species variation
            uint32_t inst_seed = master_seed + static_cast<uint32_t>(si * 1000 + c * 7);
            std::mt19937 inst_rng(inst_seed);
            veggen::PlantTraits t = make_traits_from_profile(sp, inst_seed, inst_rng);
            sprites[si][c] = veggen::generate_plant_sprite(t, col_specs[c].phase,
                                                            col_specs[c].health);
            if (sprites[si][c].w > max_w) max_w = sprites[si][c].w;
            if (sprites[si][c].h > max_h) max_h = sprites[si][c].h;
        }
    }

    int cell_w = (max_w + PADDING) * scale;
    int cell_h = (max_h + PADDING) * scale;
    int comp_w = cell_w * COLS;
    int comp_h = cell_h * num_rows;

    SDL_Surface* composite =
        SDL_CreateRGBSurfaceWithFormat(0, comp_w, comp_h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(composite, nullptr,
                 SDL_MapRGBA(composite->format, BG_COLOR & 0xFF, (BG_COLOR >> 8) & 0xFF,
                             (BG_COLOR >> 16) & 0xFF, (BG_COLOR >> 24) & 0xFF));

    int row_idx = 0;
    for (int si = 0; si < NUM_SPECIES; ++si) {
        if (filter_species >= 0 && si != filter_species)
            continue;

        for (int c = 0; c < COLS; ++c) {
            SDL_Surface* scaled = upscale(sprites[si][c], scale);
            int ox = c * cell_w + (cell_w - scaled->w) / 2;
            int oy = row_idx * cell_h + (cell_h - scaled->h) / 2;
            SDL_Rect dst_rect{ox, oy, scaled->w, scaled->h};
            SDL_BlitSurface(scaled, nullptr, composite, &dst_rect);
            SDL_FreeSurface(scaled);
        }
        ++row_idx;
    }

    const char* output_path = "spritetest_output.bmp";
    if (SDL_SaveBMP(composite, output_path) == 0) {
        std::printf("Saved %dx%d composite to %s\n", comp_w, comp_h, output_path);
        std::printf("Cols: ");
        for (int c = 0; c < COLS; ++c)
            std::printf("%s%s", col_specs[c].label, c < COLS - 1 ? " | " : "\n");
        std::printf("Rows:\n");
        for (int si = 0; si < NUM_SPECIES; ++si) {
            if (filter_species >= 0 && si != filter_species)
                continue;
            std::printf("  %2d: %s\n", si, SPECIES[si].name);
        }
    } else {
        std::fprintf(stderr, "SDL_SaveBMP failed: %s\n", SDL_GetError());
    }

    SDL_FreeSurface(composite);
    SDL_Quit();
    return 0;
}

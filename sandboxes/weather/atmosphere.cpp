#include "atmosphere.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "dynamics.h"

namespace sandbox {

// ── Constants ──────────────────────────────────────────────────────────

static constexpr float PI = 3.14159265358979f;

// Temperature
static constexpr float K_SOLAR =
    8.0f;  // solar heating strength (compensates for lack of vertical Hadley cell)
static constexpr float K_RAD =
    0.09f;  // radiative cooling rate (tuned down from 0.15 to reduce energy drain)
static constexpr float K_GROUND_AIR =
    0.25f;  // ground-air coupling (stronger — air follows ground gradient)
static constexpr float K_AIR_GROUND = 0.15f;    // air-ground coupling (reverse)
static constexpr float K_EVAP_COOL = 1.5f;      // evaporative cooling
static constexpr float LAPSE_PER_ELEV = 35.0f;  // temp drop per unit elevation
static constexpr float K_LATENT = 8.0f;         // latent heat per unit condensation (C per unit q)

// Pressure / wind
static constexpr float K_DRAG = 1.0f;  // terrain roughness drag coefficient
static constexpr float P_TEMP_COEFF =
    0.8f;  // pressure response to temperature (for p_base display mapping)
static constexpr float K_TURBULENCE = 0.25f;  // thermal turbulence strength (sub-grid gusts)

// Advection
static constexpr float ADVECT_SPEED =
    4.0f;  // advection scaling (increased from 0.5 — features must visibly move on coarse grid)

// Moisture
static constexpr float OCEAN_EVAP_RATE =
    0.08f;  // ocean moisture source per day (reduced from 0.10 — narrower ocean/land gap)
static constexpr float LAND_EVAP_RATE =
    0.07f;  // land moisture source per day (raised from 0.04 — stronger land recycling)
static constexpr float CLOUD_PRECIP_RATE = 0.9f;  // cloud → precipitation per day (reduced from 1.5
                                                   // — lets clouds advect further inland)
static constexpr float CLOUD_PRECIP_THRESHOLD =
    0.08f;  // minimum cloud for precipitation (raised from 0.06 — favors organized events)
static constexpr float CLOUD_EVAP_BASE =
    0.30f;  // base cloud dissipation per day (increased — clouds must clear between storms)
static constexpr float CLOUD_EVAP_DRY = 0.50f;      // additional dissipation in dry air
static constexpr float CLOUD_SUBSIDE_RATE = 0.80f;  // cloud clearing from high-pressure subsidence
static constexpr float UPLIFT_STRENGTH = 2.0f;      // orographic uplift multiplier (reduced from 4.0)

// Wind-temperature coupling
static constexpr float K_CONVECTIVE = 0.08f;  // wind convective cooling/heating on ground

// Diffusion (moisture spread via gradients — T smoothing handled by eddy persistence decay)
static constexpr float K_MOIST_DIFFUSE = 0.03f;  // moisture diffusion rate per day

// Numerical stability (invariant 10: Courant condition)
static constexpr float MAX_WIND_SPEED = 6.0f;  // hard cap on wind speed (cells/day)
static constexpr float MAX_TRACE_DIST = 1.5f;  // max advection trace per step (cells)

// Convective instability (invariant 7: storms release energy)
static constexpr float CONVECTIVE_THRESHOLD = 9.0f;  // ground-air T diff triggering convection (C)
static constexpr float CONVECTIVE_PRECIP = 0.15f;    // enhanced precip rate during convection

// Phase 0: terrain coupling constants
static constexpr float K_ASPECT_SOLAR = 2.0f;  // aspect-based solar heating strength

// Phase 1: moisture budget constants
static constexpr float BUDGET_OCEAN_RECHARGE =
    0.06f;  // budget recharge over ocean per day (reduced from 0.08)
static constexpr float BUDGET_LAND_RECHARGE = 0.04f;  // land recharge (raised from 0.01)
static constexpr float BUDGET_RAIN_COST =
    3.0f;  // budget consumed per unit precipitation (tuned up from 1.5)
static constexpr float BUDGET_MIXING = 0.005f;  // atmospheric mixing slowly restores budget

// Phase 2: anomaly relaxation constants
static constexpr float K_ANOM_RELAX_T =
    0.06f;  // T anomaly decay per day (~17d half-life, tuned up from 0.02)
static constexpr float K_ANOM_RELAX_Q = 0.01f;  // q anomaly decay per day
static constexpr float K_ANOM_RELAX_P = 0.03f;  // p anomaly decay per day
static constexpr float BASELINE_UPDATE_RATE =
    0.05f;  // seasonal baseline shift rate (~14d half-life)

// Phase 3: two-layer atmosphere constants
static constexpr float K_VERT_HEAT =
    0.02f;  // vertical heat exchange per day (tuned down from 0.04)
static constexpr float K_VERT_MOIST = 0.05f;          // vertical moisture exchange per day
static constexpr float CONVECTIVE_VERT_BOOST = 3.0f;  // enhanced exchange during instability
static constexpr float INVERSION_THRESHOLD = 2.0f;    // stability > this = inversion
static constexpr float EXPECTED_LAPSE =
    -18.0f;  // expected T_upper - T (tuned from -25 to allow inversions)
static constexpr float UPPER_WIND_FACTOR = 1.5f;  // upper/lower wind ratio (used in init)
static constexpr float UPPER_ADVECT_SPEED =
    6.0f;  // upper advection (increased from 0.8 for visible motion)

// QG dynamics constants (ADR 0012)
static constexpr float QG_BETA = 0.873f;                    // beta-plane at 45 deg (1/day/cell)
static constexpr float QG_Ld = 5.0f;                        // deformation radius in cells
static constexpr float QG_invLd2 = 1.0f / (QG_Ld * QG_Ld);  // 0.04
static constexpr float QG_SHEAR_GAIN = 3.0f;  // (cells/day) per (degC/cell) thermal wind
static constexpr float QG_C_T = 2.0f;         // degC per psi unit for T_anom mapping
static constexpr float QG_EKMAN_R = 0.02f;    // 1/day Rayleigh drag on lower layer
static constexpr int QG_SOR_ITERS = 200;      // SOR iterations for PV inversion
static constexpr float QG_SOR_OMEGA = 1.6f;   // SOR over-relaxation parameter

// Stochastic eddy parameterization constants
// Instead of advecting PV (which numerical diffusion kills on a 64x32 grid),
// we generate weather-scale PV perturbations stochastically, modulated by the
// local baroclinicity (temperature gradient). PV inversion then gives balanced
// winds. This produces visible, localized, temporary weather systems.
static constexpr float EDDY_AMPLITUDE =
    4.0f;  // PV perturbation amplitude — must be strong enough
           // that psi_bc gradients produce winds comparable to U_s
static constexpr float EDDY_PERSIST =
    0.95f;  // per-tick persistence (α) — shorter-lived = less upscale accumulation
static constexpr float EDDY_DRIFT_SPEED = 0.3f;  // fraction of mean flow for eddy advective drift
static constexpr int EDDY_CELL_SIZE =
    4;  // noise cell size — smaller = more synoptic-scale variance

// Cheap hash noise for turbulence — returns [-1, +1]
static float turb_noise(uint32_t x, uint32_t y, uint32_t t) {
    uint32_t h = x * 374761393u + y * 668265263u + t * 1274126177u;
    h = (h ^ (h >> 13)) * 1103515245u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFF) / 32767.5f - 1.0f;
}

// Hash noise at integer coords, returns [0, 1]
static float hash01(uint32_t x, uint32_t y, uint32_t t) {
    uint32_t h = x * 374761393u + y * 668265263u + t * 1274126177u;
    h = (h ^ (h >> 13)) * 1103515245u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}

// Smooth bilinear noise for weather fronts — returns [0, 1]
// cell_size controls spatial scale (larger = bigger weather systems)
static float smooth_noise(float wx, float wy, int time_step, int cell_size) {
    float cx = wx / static_cast<float>(cell_size);
    float cy = wy / static_cast<float>(cell_size);
    int ix = static_cast<int>(std::floor(cx));
    int iy = static_cast<int>(std::floor(cy));
    float fx = cx - static_cast<float>(ix);
    float fy = cy - static_cast<float>(iy);
    // Smooth interpolation (cubic Hermite)
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);

    auto ux = static_cast<uint32_t>(ix);
    auto uy = static_cast<uint32_t>(iy);
    auto ut = static_cast<uint32_t>(time_step);
    float n00 = hash01(ux, uy, ut);
    float n10 = hash01(ux + 1, uy, ut);
    float n01 = hash01(ux, uy + 1, ut);
    float n11 = hash01(ux + 1, uy + 1, ut);

    float nx0 = n00 + (n10 - n00) * fx;
    float nx1 = n01 + (n11 - n01) * fx;
    return nx0 + (nx1 - nx0) * fy;
}

// ── Energy computation helper (Phase 4) ─────────────────────────────────
// E = sum(cp * T_lower + cp * T_upper + latent * cloud) per cell
static constexpr float CP_AIR = 1.0f;         // normalized heat capacity
static constexpr float LATENT_ENERGY = 8.0f;  // matches K_LATENT

static float compute_total_energy(const std::vector<AtmosphereCell>& cells) {
    double energy = 0.0;
    for (const auto& c : cells) {
        energy += CP_AIR * c.T;
        energy += CP_AIR * c.T_upper;
        energy += LATENT_ENERGY * c.cloud;
    }
    return static_cast<float>(energy);
}

// ── QG helper: PV inversion via SOR ──────────────────────────────────
// Solves: q1 = del2(psi1) + invLd2*(psi2 - psi1) + beta*y
//         q2 = del2(psi2) + invLd2*(psi1 - psi2) + beta*y
// Given q1,q2, solve for psi1,psi2.
// x: periodic, y: wall (psi=0 at boundaries)

static void qg_pv_inversion(std::vector<AtmosphereCell>& cells, uint32_t w, uint32_t h) {
    // SOR iteration to solve the coupled elliptic system
    // Rearranging: del2(psi1) - invLd2*psi1 = q1 - invLd2*psi2 - beta*y
    //              del2(psi2) - invLd2*psi2 = q2 - invLd2*psi1 - beta*y
    // 5-point Laplacian: del2(psi) ≈ psi(i-1,j) + psi(i+1,j) + psi(i,j-1) + psi(i,j+1) - 4*psi(i,j)
    // So: (psi_L + psi_R + psi_U + psi_D - 4*psi) - invLd2*psi = rhs
    //     psi = (psi_L + psi_R + psi_U + psi_D - rhs) / (4 + invLd2)

    float inv_diag = 1.0f / (4.0f + QG_invLd2);

    for (int iter = 0; iter < QG_SOR_ITERS; ++iter) {
        for (uint32_t ay = 1; ay + 1 < h; ++ay) {
            float beta_y = QG_BETA * static_cast<float>(ay);
            for (uint32_t ax = 0; ax < w; ++ax) {
                uint32_t ax_l = (ax == 0) ? w - 1 : ax - 1;
                uint32_t ax_r = (ax == w - 1) ? 0 : ax + 1;
                uint32_t ay_u = ay - 1;
                uint32_t ay_d = ay + 1;

                auto& c = cells[ay * w + ax];
                float psi1_L = cells[ay * w + ax_l].psi1;
                float psi1_R = cells[ay * w + ax_r].psi1;
                float psi1_U = cells[ay_u * w + ax].psi1;
                float psi1_D = cells[ay_d * w + ax].psi1;

                float psi2_L = cells[ay * w + ax_l].psi2;
                float psi2_R = cells[ay * w + ax_r].psi2;
                float psi2_U = cells[ay_u * w + ax].psi2;
                float psi2_D = cells[ay_d * w + ax].psi2;

                // RHS for layer 1: q1 - invLd2*psi2 - beta*y
                float rhs1 = c.q1_pv - QG_invLd2 * c.psi2 - beta_y;
                float new_psi1 = (psi1_L + psi1_R + psi1_U + psi1_D - rhs1) * inv_diag;
                c.psi1 += QG_SOR_OMEGA * (new_psi1 - c.psi1);

                // RHS for layer 2: q2 - invLd2*psi1 - beta*y
                // Use updated psi1 (Gauss-Seidel)
                float rhs2 = c.q2_pv - QG_invLd2 * c.psi1 - beta_y;
                float new_psi2 = (psi2_L + psi2_R + psi2_U + psi2_D - rhs2) * inv_diag;
                c.psi2 += QG_SOR_OMEGA * (new_psi2 - c.psi2);
            }
        }
        // Enforce wall boundaries: psi = 0 at y=0 and y=h-1
        for (uint32_t ax = 0; ax < w; ++ax) {
            cells[ax].psi1 = 0.0f;
            cells[ax].psi2 = 0.0f;
            cells[(h - 1) * w + ax].psi1 = 0.0f;
            cells[(h - 1) * w + ax].psi2 = 0.0f;
        }
    }
}

// (Del-4 hyperviscosity removed — not needed with stochastic eddy parameterization.
//  Stochastic PV generation replaces PV advection, so there's no grid-scale noise
//  to suppress. Eddy amplitude is controlled by the persistence parameter instead.)

// ── Init ───────────────────────────────────────────────────────────────

void init_atmosphere(AtmosphereState& atmo, const Terrain& world, const ClimateData& climate,
                     uint32_t scale) {
    atmo.scale = scale;
    atmo.width = (world.width + scale - 1) / scale;
    atmo.height = (world.height + scale - 1) / scale;
    atmo.time_of_day = 12.0f;
    atmo.day_of_year = 80.0f;

    size_t num_cells = static_cast<size_t>(atmo.width) * atmo.height;
    atmo.cells.resize(num_cells);
    atmo.scratch.resize(num_cells);

    for (uint32_t ay = 0; ay < atmo.height; ++ay) {
        for (uint32_t ax = 0; ax < atmo.width; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);

            float sum_T = 0, sum_q = 0, sum_u = 0, sum_v = 0;
            float sum_elev = 0, sum_rough = 0;
            float sum_slope = 0, sum_aspect_sin = 0, sum_aspect_cos = 0;
            float sum_dist_ocean = 0;
            int count = 0;
            int water_count = 0;

            for (uint32_t dy = 0; dy < scale; ++dy) {
                for (uint32_t dx = 0; dx < scale; ++dx) {
                    uint32_t tx = ax * scale + dx;
                    uint32_t ty = ay * scale + dy;
                    if (tx >= world.width || ty >= world.height)
                        continue;

                    const auto& ct = climate.tile_at(tx, ty);
                    const auto& tt = world.tile_at(tx, ty);

                    sum_T += ct.temperature;
                    sum_q += ct.moisture;
                    sum_u += ct.wind_u;
                    sum_v += ct.wind_v;
                    sum_elev += tt.elev01;
                    sum_rough += tt.roughness;
                    sum_slope += tt.slope01;
                    sum_aspect_sin += std::sin(tt.aspect);
                    sum_aspect_cos += std::cos(tt.aspect);
                    sum_dist_ocean += tt.dist_ocean;
                    if (tt.is_ocean || tt.band == ElevBand::Water)
                        ++water_count;
                    ++count;
                }
            }

            if (count > 0) {
                float n = static_cast<float>(count);
                cell.T = sum_T / n;
                cell.u = sum_u / n;
                cell.v = sum_v / n;
                cell.avg_elevation = sum_elev / n;
                cell.avg_roughness = sum_rough / n;
                cell.avg_slope = sum_slope / n;
                cell.avg_aspect = std::atan2(sum_aspect_sin / n, sum_aspect_cos / n);
                cell.avg_dist_ocean = sum_dist_ocean / n;
                cell.is_water = water_count > count / 2;

                // Initialize q at saturation for the local temperature.
                // Baked moisture is a normalized index [0,1], NOT specific humidity.
                // Cap at q_sat so we don't get a catastrophic initial condensation burst.
                float q_sat = std::clamp(0.05f * std::exp(0.06f * cell.T), 0.02f, 1.0f);
                float baked_moisture = sum_q / n;
                cell.q = std::clamp(baked_moisture * q_sat, 0.0f, q_sat);
            }

            // Phase 2: set baselines from baked values
            cell.T_base = cell.T;
            cell.T_anom = 0.0f;
            cell.q_base = cell.q;
            cell.q_anom = 0.0f;
            cell.p_base = 1013.0f;
            cell.p_anom = 0.0f;

            // Phase 3: upper layer init — lapse rate from surface
            cell.T_upper = cell.T - 25.0f;  // ~6.5C/km × ~4km
            cell.q_upper = cell.q * 0.3f;   // drier aloft
            cell.u_upper = cell.u * UPPER_WIND_FACTOR;
            cell.v_upper = cell.v * UPPER_WIND_FACTOR;
            cell.p_upper = 500.0f - 0.5f * (cell.T_upper + 10.0f);
            cell.stability = cell.T_upper - cell.T;

            cell.ground_temp = cell.T;
            cell.p = 1013.0f;
            cell.cloud = 0.0f;
            cell.precip_rate = 0.0f;
            cell.avg_snow = 0.0f;
            cell.avg_soil_wet = cell.is_water ? 1.0f : cell.q;
            // Phase 1: init moisture budget based on proximity to ocean
            if (cell.is_water) {
                cell.precip_budget = 1.0f;
            } else {
                float coast_factor = 1.0f / (1.0f + cell.avg_dist_ocean * 0.05f);
                cell.precip_budget = 0.5f + 0.3f * coast_factor;
            }
        }
    }

    // QG initialization: set PV from beta*y + synoptic-scale perturbation
    // Perturbation must be at synoptic scale (wavenumber 3-6),
    // NOT grid-scale hash noise. Use smooth noise at 8-16 cell wavelength.
    for (uint32_t ay = 0; ay < atmo.height; ++ay) {
        float beta_y = QG_BETA * static_cast<float>(ay);
        // Perturbation envelope: strongest at mid-latitudes (rows 8-24)
        float y_frac = static_cast<float>(ay) / static_cast<float>(atmo.height);
        float envelope = std::sin(y_frac * PI);  // zero at walls, max at center
        envelope *= envelope;                    // sharper peak
        for (uint32_t ax = 0; ax < atmo.width; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);
            cell.psi1 = 0.0f;
            cell.psi2 = 0.0f;
            // Multi-scale synoptic perturbation (wavenumber 3-6 in x)
            float fax = static_cast<float>(ax);
            float fay = static_cast<float>(ay);
            float perturb = 0.0f;
            // k=3 mode
            perturb += 0.4f * std::sin(2.0f * PI * 3.0f * fax / static_cast<float>(atmo.width) +
                                       turb_noise(0, ay, 42) * PI);
            // k=5 mode
            perturb += 0.3f * std::sin(2.0f * PI * 5.0f * fax / static_cast<float>(atmo.width) +
                                       turb_noise(1, ay, 43) * PI);
            // k=4 mode with y-variation
            perturb += 0.3f * std::sin(2.0f * PI * 4.0f * fax / static_cast<float>(atmo.width) +
                                       2.0f * PI * fay / static_cast<float>(atmo.height));
            // Smooth noise for some randomness at 8-cell scale
            perturb += smooth_noise(fax, fay, 42, 8) * 0.5f - 0.25f;

            float amplitude = 0.5f * envelope;  // moderate initial perturbation
            cell.q1_pv = beta_y + perturb * amplitude;
            cell.q2_pv = beta_y + perturb * amplitude * 0.3f;  // weaker lower layer
        }
    }

    // Phase 4: record initial energy for drift tracking
    atmo.initial_energy = compute_total_energy(atmo.cells);
    atmo.total_energy = atmo.initial_energy;
}

// ── Bilinear helper for advection ──────────────────────────────────────

static float sample_field(const std::vector<AtmosphereCell>& buf, uint32_t w, uint32_t h, float fx,
                          float fy, float AtmosphereCell::* field) {
    // x: periodic wrap; y: clamp at walls
    float fw = static_cast<float>(w);
    fx = std::fmod(fx, fw);
    if (fx < 0.0f)
        fx += fw;
    fy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));

    auto x0 = static_cast<int>(fx);
    auto y0 = static_cast<int>(fy);
    int x1 = (x0 + 1) % static_cast<int>(w);  // periodic wrap
    int y1 = std::min(y0 + 1, static_cast<int>(h - 1));
    float sx = fx - static_cast<float>(x0);
    float sy = fy - static_cast<float>(y0);

    float v00 = buf[static_cast<size_t>(y0) * w + static_cast<size_t>(x0)].*field;
    float v10 = buf[static_cast<size_t>(y0) * w + static_cast<size_t>(x1)].*field;
    float v01 = buf[static_cast<size_t>(y1) * w + static_cast<size_t>(x0)].*field;
    float v11 = buf[static_cast<size_t>(y1) * w + static_cast<size_t>(x1)].*field;

    return (v00 * (1.0f - sx) + v10 * sx) * (1.0f - sy) + (v01 * (1.0f - sx) + v11 * sx) * sy;
}

// ── Tick ───────────────────────────────────────────────────────────────

// Helper: compute variance of a field across all cells
static float field_variance(const std::vector<AtmosphereCell>& cells,
                            float AtmosphereCell::* field) {
    if (cells.empty())
        return 0.0f;
    double sum = 0, sum2 = 0;
    for (const auto& c : cells) {
        double v = c.*field;
        sum += v;
        sum2 += v * v;
    }
    double n = static_cast<double>(cells.size());
    double mean = sum / n;
    return static_cast<float>(sum2 / n - mean * mean);
}

void tick_atmosphere(AtmosphereState& atmo, const Terrain& world, const DynamicState& dyn,
                     float dt_seconds, TickVarianceBudget* diag) {
    float dt_days = dt_seconds * dyn.time_scale * DAYS_PER_SECOND;
    if (dt_days <= 0.0f || dyn.paused || atmo.cells.empty())
        return;

    // Cap dt to prevent instability
    dt_days = std::min(dt_days, 2.0f);
    atmo.last_dt_days = dt_days;

    uint32_t aw = atmo.width;
    uint32_t ah = atmo.height;
    uint32_t scale = atmo.scale;

    // ── Advance clock ──────────────────────────────────────────────────
    float dt_hours = dt_days * 24.0f;
    atmo.time_of_day += dt_hours;
    while (atmo.time_of_day >= 24.0f) {
        atmo.time_of_day -= 24.0f;
        atmo.day_of_year += 1.0f;
    }
    while (atmo.day_of_year >= 365.0f)
        atmo.day_of_year -= 365.0f;

    // ── Variance snapshot: start of tick ────────────────────────────────
    if (diag) {
        diag->valid = true;
        diag->T_anom_var[0] = field_variance(atmo.cells, &AtmosphereCell::T_anom);
        diag->q_anom_var[0] = field_variance(atmo.cells, &AtmosphereCell::q_anom);
        diag->p_anom_var[0] = field_variance(atmo.cells, &AtmosphereCell::p_anom);
    }

    // ── Step 1: Update terrain coupling from dynamic state ─────────────
    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);
            float sum_snow = 0, sum_wet = 0;
            int count = 0;

            for (uint32_t dy = 0; dy < scale; ++dy) {
                for (uint32_t dx = 0; dx < scale; ++dx) {
                    uint32_t tx = ax * scale + dx;
                    uint32_t ty = ay * scale + dy;
                    if (tx >= world.width || ty >= world.height)
                        continue;
                    const auto& dt = dyn.tile_at(tx, ty);
                    sum_snow += dt.snow_depth;
                    sum_wet += dt.soil_moisture + dt.surface_water;
                    ++count;
                }
            }
            if (count > 0) {
                float n = static_cast<float>(count);
                cell.avg_snow = sum_snow / n;
                cell.avg_soil_wet = sum_wet / n;
            }
        }
    }

    // ── Precompute solar/seasonal values (used later in temperature forcing) ──
    float season_angle = (atmo.day_of_year - 80.0f) / 365.0f * 2.0f * PI;
    float season_offset = std::sin(season_angle);  // +1 summer, -1 winter
    float diurnal_angle = (atmo.time_of_day - 14.0f) / 24.0f * 2.0f * PI;
    float diurnal = std::cos(diurnal_angle);  // +1 afternoon, -1 night

    // ── Step 1b: Seasonal baseline update (Phase 2) ─────────────────────
    // Slowly shift T_base toward the current seasonal T_ref so baselines
    // track seasons. q_base updates from evaporation/precipitation shifts.
    for (uint32_t ay = 0; ay < ah; ++ay) {
        float lat_frac = static_cast<float>(ay) / static_cast<float>(ah);
        float latitude = (0.5f - lat_frac) * 2.0f;
        float solar_base_lat = std::max(0.0f, 1.0f - latitude * latitude * 0.8f);
        float solar_seasonal_lat = solar_base_lat + latitude * season_offset * 0.35f;
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);
            float T_ref_base = -15.0f + solar_seasonal_lat * 48.0f;
            T_ref_base -= cell.avg_elevation * LAPSE_PER_ELEV;
            cell.T_base += BASELINE_UPDATE_RATE * (T_ref_base - cell.T_base) * dt_days;

            // Seasonal q_base tracking: moisture baseline follows temperature-derived
            // Clausius-Clapeyron reference scaled by distance from ocean
            float q_sat_ref = std::clamp(0.05f * std::exp(0.06f * T_ref_base), 0.02f, 1.0f);
            float maritime_factor = 1.0f / (1.0f + cell.avg_dist_ocean * 0.015f);
            float q_ref = q_sat_ref * (0.30f + 0.45f * maritime_factor);  // RH ~30-75% baseline
            cell.q_base += BASELINE_UPDATE_RATE * (q_ref - cell.q_base) * dt_days;
            cell.q_base = std::clamp(cell.q_base, 0.01f, 0.5f);
        }
    }

    // ── Step 2: QG Dynamics — PV advection, inversion, balanced winds ───
    // Replaces old pressure+PGF+Coriolis+damping wind model with a two-layer
    // quasi-geostrophic dynamical core that produces baroclinic instability.

    // 2a: Compute background thermal wind shear U_s(y) from dT_base/dy
    // The thermal wind relation: vertical wind shear ∝ horizontal temperature gradient
    float U_shear[64] = {};                                   // stack-allocated, max grid height 64
    static_assert(sizeof(U_shear) >= sizeof(float) * 1, "");  // always valid
    for (uint32_t ay = 1; ay + 1 < ah; ++ay) {
        float dTdy = 0.0f;
        for (uint32_t ax = 0; ax < aw; ++ax) {
            dTdy += atmo.cell_at(ax, ay + 1).T_base - atmo.cell_at(ax, ay - 1).T_base;
        }
        dTdy /= static_cast<float>(aw) * 2.0f;  // zonal mean gradient (degC/cell)
        U_shear[ay] = -QG_SHEAR_GAIN * dTdy;    // thermal wind: shear proportional to -dT/dy
    }
    U_shear[0] = U_shear[1];
    U_shear[ah - 1] = U_shear[ah - 2];

    // 2b: Stochastic baroclinic eddy parameterization
    // On a 64x32 grid, semi-Lagrangian numerical diffusion (~0.14/day at synoptic
    // scale) exceeds the Eady growth rate (~0.13/day), killing perturbation growth.
    // Instead, we GENERATE weather-scale PV perturbations stochastically, modulated
    // by the local baroclinicity (dT/dy). This produces localized, temporary weather
    // systems without requiring actual instability growth through advection.
    //
    // The PV field is: q = beta*y + eddy_perturbation
    // We evolve eddy_perturbation with: persistence × old + (1-persistence) × target
    // where target comes from smooth noise at synoptic scale, amplitude ∝ |dT/dy|.
    // PV inversion (step 2e) and wind diagnosis (step 2f) remain unchanged.

    {
        // Temporal phase for noise evolution — changes every ~6 hours for weather timescale
        float noise_phase = atmo.day_of_year * 4.0f + atmo.time_of_day / 6.0f;
        int noise_t0 = static_cast<int>(std::floor(noise_phase));
        int noise_t1 = noise_t0 + 1;
        float noise_blend = noise_phase - static_cast<float>(noise_t0);
        // Smooth crossfade (cubic Hermite)
        noise_blend = noise_blend * noise_blend * (3.0f - 2.0f * noise_blend);

        // Eddy drift: shift noise sample coordinates eastward with mean flow
        // This makes weather systems move with the background flow
        float drift_x = atmo.day_of_year * 2.0f;  // slow eastward drift (~2 cells/day)

        float persist = std::max(
            0.3f,
            std::pow(EDDY_PERSIST, dt_days * 30.0f));  // ~30 ticks/day, floored for high time_scale

        for (uint32_t ay = 1; ay + 1 < ah; ++ay) {
            float beta_y = QG_BETA * static_cast<float>(ay);
            float U_s = U_shear[ay];
            float baroclinicity = std::abs(U_s);  // stronger gradient → stronger eddies

            // Latitude envelope: strongest at mid-latitudes
            float y_frac = static_cast<float>(ay) / static_cast<float>(ah);
            float envelope = std::sin(y_frac * PI);
            envelope *= envelope;

            // Amplitude scales with baroclinicity and latitude envelope
            float amp = EDDY_AMPLITUDE * baroclinicity * envelope;
            amp = std::min(amp, 6.0f);  // prevent extreme values

            for (uint32_t ax = 0; ax < aw; ++ax) {
                auto& c = atmo.cells[ay * aw + ax];

                // Sample smooth noise at synoptic scale with drift
                float sample_x = static_cast<float>(ax) + drift_x * EDDY_DRIFT_SPEED;
                float sample_y = static_cast<float>(ay);

                // Multi-scale noise: primary (8-cell) + secondary (12-cell) + fine (5-cell)
                // Two time steps blended for smooth temporal evolution
                float n0_a = smooth_noise(sample_x, sample_y, noise_t0, EDDY_CELL_SIZE);
                float n1_a = smooth_noise(sample_x, sample_y, noise_t1, EDDY_CELL_SIZE);
                float n0_b = smooth_noise(sample_x + 500.0f, sample_y + 500.0f, noise_t0 + 73, 12);
                float n1_b = smooth_noise(sample_x + 500.0f, sample_y + 500.0f, noise_t1 + 73, 12);
                float n0_c =
                    smooth_noise(sample_x * 1.5f + 200.0f, sample_y * 1.5f, noise_t0 + 137, 5);
                float n1_c =
                    smooth_noise(sample_x * 1.5f + 200.0f, sample_y * 1.5f, noise_t1 + 137, 5);

                float noise_a = n0_a + (n1_a - n0_a) * noise_blend;
                float noise_b = n0_b + (n1_b - n0_b) * noise_blend;
                float noise_c = n0_c + (n1_c - n0_c) * noise_blend;

                // Combine: centered on 0, range roughly [-1, +1]
                float target_pv = (noise_a * 0.5f + noise_b * 0.3f + noise_c * 0.2f - 0.5f) * 2.0f;
                target_pv *= amp;

                // The eddy PV perturbation is q_pv - beta*y
                float eddy1 = c.q1_pv - beta_y;
                float eddy2 = c.q2_pv - beta_y;

                // Evolve with persistence: blend old eddy with target
                // Upper layer: target PV perturbation (cyclone/anticyclone)
                float new_eddy1 = eddy1 * persist + target_pv * (1.0f - persist);
                // Lower layer: OPPOSITE sign creates baroclinic tilt — the key for
                // generating temperature anomalies through psi1-psi2 difference
                float new_eddy2 = eddy2 * persist + (-target_pv * 0.6f) * (1.0f - persist);

                c.q1_pv = beta_y + new_eddy1;
                c.q2_pv = beta_y + new_eddy2;
            }
        }
    }

    // 2c: Ekman drag on lower layer PV (surface friction)
    // Kept from QG — spins down lower-layer vorticity, creating surface convergence
    for (uint32_t ay = 1; ay + 1 < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& c = atmo.cells[ay * aw + ax];
            uint32_t ax_l = (ax == 0) ? aw - 1 : ax - 1;
            uint32_t ax_r = (ax == aw - 1) ? 0 : ax + 1;
            float lap_psi2 = atmo.cells[ay * aw + ax_l].psi2 + atmo.cells[ay * aw + ax_r].psi2 +
                             atmo.cells[(ay - 1) * aw + ax].psi2 +
                             atmo.cells[(ay + 1) * aw + ax].psi2 - 4.0f * c.psi2;
            c.q2_pv -= QG_EKMAN_R * lap_psi2 * dt_days;
        }
    }

    // 2e: PV inversion — solve for psi1, psi2 from q1_pv, q2_pv
    qg_pv_inversion(atmo.cells, aw, ah);

    // 2f: Diagnose winds from streamfunction and export to cell.u/v
    for (uint32_t ay = 0; ay < ah; ++ay) {
        float U_s = U_shear[ay];
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);

            if (ay == 0 || ay == ah - 1) {
                // Wall boundary: no normal flow (v=0), gentle time-scaled u damping
                float wall_damp = std::max(0.0f, 1.0f - 2.0f * dt_days);
                cell.u *= wall_damp;
                cell.v = 0.0f;
                cell.u_upper *= wall_damp;
                cell.v_upper = 0.0f;
                continue;
            }

            uint32_t ax_l = (ax == 0) ? aw - 1 : ax - 1;
            uint32_t ax_r = (ax == aw - 1) ? 0 : ax + 1;

            // Winds from pressure gradient force + Coriolis balance
            // Upper layer: jet stream from thermal wind + eddy geostrophic winds
            auto psi_bc_of = [](const AtmosphereCell& c) { return (c.psi1 - c.psi2) * 0.5f; };
            float bc_U = psi_bc_of(atmo.cell_at(ax, ay - 1));
            float bc_D = psi_bc_of(atmo.cell_at(ax, ay + 1));
            float bc_L = psi_bc_of(atmo.cell_at(ax_l, ay));
            float bc_R = psi_bc_of(atmo.cell_at(ax_r, ay));

            float u1 = -(bc_D - bc_U) * 0.5f + U_s * 0.5f;
            float v1 = (bc_R - bc_L) * 0.5f;

            // Lower layer: pressure-gradient driven winds from p_anom
            // Surface winds are NOT geostrophic from psi (too smooth on coarse grid).
            // Instead, compute from the PRESSURE GRADIENT of p_anom directly.
            // Wind blows from high to low pressure, deflected by Coriolis:
            //   u = -(1/f) * dp/dy  (geostrophic)
            //   v =  (1/f) * dp/dx  (geostrophic)
            // Plus cross-isobar flow toward low pressure (Ekman surface friction):
            //   u += -K_cross * dp/dx
            //   v += -K_cross * dp/dy
            static constexpr float K_PGF =
                0.6f;  // pressure-to-wind scaling (cells/day per hPa/cell)
            static constexpr float K_CROSS = 0.25f;  // cross-isobar (friction) fraction

            float p_U = atmo.cell_at(ax, ay - 1).p_anom;
            float p_D = atmo.cell_at(ax, ay + 1).p_anom;
            float p_L = atmo.cell_at(ax_l, ay).p_anom;
            float p_R = atmo.cell_at(ax_r, ay).p_anom;
            float dpdy = (p_D - p_U) * 0.5f;
            float dpdx = (p_R - p_L) * 0.5f;

            // Latitude-dependent Coriolis (stronger at poles, weaker at equator)
            float lat_frac = static_cast<float>(ay) / static_cast<float>(ah);
            float latitude = (0.5f - lat_frac) * 2.0f;
            float coriolis = std::max(0.1f, std::abs(latitude));
            float sign_f = (latitude >= 0.0f) ? 1.0f : -1.0f;

            // Geostrophic: wind perpendicular to pressure gradient
            // Cross-isobar: wind toward low pressure (friction turns wind)
            float u2 = -K_PGF * dpdy * sign_f / coriolis - K_CROSS * K_PGF * dpdx / coriolis;
            float v2 = K_PGF * dpdx * sign_f / coriolis - K_CROSS * K_PGF * dpdy / coriolis;
            // Small mean westerly drift (surface expression of jet stream)
            u2 += U_s * 0.1f;

            // Terrain drag on lower layer only
            float drag = 1.0f / (1.0f + K_DRAG * cell.avg_roughness * dt_days);
            u2 *= drag;
            v2 *= drag;

            // Small-scale turbulence on lower layer (sub-grid convective gusts)
            float convective_strength = (cell.is_water ? 0.4f : 1.0f);
            float daytime_boost = 0.5f + 0.5f * std::max(0.0f, diurnal);
            uint32_t turb_fast =
                static_cast<uint32_t>(atmo.time_of_day * 1.0f + atmo.day_of_year * 24.0f);
            float tu = turb_noise(ax, ay, turb_fast) * 0.6f +
                       turb_noise(ax * 3u, ay * 3u,
                                  static_cast<uint32_t>(atmo.time_of_day * 0.17f +
                                                        atmo.day_of_year * 4.0f)) *
                           0.4f;
            float tv = turb_noise(ax + 97u, ay + 53u, turb_fast) * 0.6f +
                       turb_noise(ax * 3u + 41u, ay * 3u + 17u,
                                  static_cast<uint32_t>(atmo.time_of_day * 0.17f +
                                                        atmo.day_of_year * 4.0f)) *
                           0.4f;
            u2 += K_TURBULENCE * 0.5f * convective_strength * daytime_boost * tu;
            v2 += K_TURBULENCE * 0.5f * convective_strength * daytime_boost * tv;

            // Export to cell fields
            cell.u = u2;
            cell.v = v2;
            cell.u_upper = u1;
            cell.v_upper = v1;

            // Hard cap wind speed
            float spd = std::sqrt(cell.u * cell.u + cell.v * cell.v);
            if (spd > MAX_WIND_SPEED) {
                float s = MAX_WIND_SPEED / spd;
                cell.u *= s;
                cell.v *= s;
            }
            float spd_u = std::sqrt(cell.u_upper * cell.u_upper + cell.v_upper * cell.v_upper);
            if (spd_u > MAX_WIND_SPEED * 1.5f) {
                float s = MAX_WIND_SPEED * 1.5f / spd_u;
                cell.u_upper *= s;
                cell.v_upper *= s;
            }

            // 2g: Map T_anom from thermal wind (QG temperature ∝ psi_bc = (psi1 - psi2)/2)
            float psi_bc_here = (cell.psi1 - cell.psi2) * 0.5f;
            float qg_T_anom =
                QG_C_T * psi_bc_here * 2.0f;  // factor 2 because psi_bc is half of (psi1-psi2)
            qg_T_anom = std::clamp(qg_T_anom, -15.0f, 15.0f);
            cell.T_anom = cell.T_anom * 0.7f + qg_T_anom * 0.3f;

            // Pressure from baroclinic streamfunction (geostrophic balance)
            float T_base_mean_local = 15.0f;
            cell.p_base = 1013.0f - P_TEMP_COEFF * (cell.T_base - T_base_mean_local);
            cell.p_base -= cell.avg_elevation * 40.0f;
            // Surface pressure anomaly from baroclinic mode (low psi_bc = low pressure)
            cell.p_anom = psi_bc_here * 2.0f;  // positive psi_bc → high pressure
            cell.p = cell.p_base + cell.p_anom;

            // Upper pressure from baroclinic mode (opposite sign — upper low over surface high)
            cell.p_upper = 500.0f + psi_bc_here * 2.0f;
        }
    }

    // ── Step 4: Semi-Lagrangian advection (Phase 2: anomalies only) ─────
    // Key: advecting anomalies instead of full fields prevents diffusion from
    // eroding the latitude temperature gradient. Baselines are fixed.
    // Invariant 2: Record atmospheric water before advection
    float water_before_advect = 0.0f;
    for (const auto& c : atmo.cells)
        water_before_advect += c.q + c.cloud;

    atmo.scratch = atmo.cells;

    auto wrap_x = [aw](float fx) -> float {
        float fw = static_cast<float>(aw);
        fx = std::fmod(fx, fw);
        if (fx < 0.0f)
            fx += fw;
        return fx;
    };

    // Displacement tracking for diagnostics
    std::vector<float> displacements, displacements_upper;
    if (diag) {
        displacements.reserve(aw * ah);
        displacements_upper.reserve(aw * ah);
        diag->cfl_clamp_count = 0;
    }

    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            const auto& src = atmo.scratch[static_cast<size_t>(ay) * aw + ax];

            float trace_x = src.u * dt_days * ADVECT_SPEED;
            float trace_y = src.v * dt_days * ADVECT_SPEED;

            // Invariant 10: CFL limiter
            float trace_dist = std::sqrt(trace_x * trace_x + trace_y * trace_y);
            if (trace_dist > MAX_TRACE_DIST) {
                float s = MAX_TRACE_DIST / trace_dist;
                trace_x *= s;
                trace_y *= s;
                if (diag)
                    diag->cfl_clamp_count++;
            }
            if (diag)
                displacements.push_back(trace_dist);

            float x0 = wrap_x(static_cast<float>(ax) - trace_x);
            float y0 = static_cast<float>(ay) - trace_y;

            auto& cell = atmo.cell_at(ax, ay);
            // Phase 2: advect anomalies only — baseline gradients preserved indefinitely
            cell.T_anom = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::T_anom);
            cell.q_anom = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::q_anom);
            cell.cloud = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::cloud);
            // Phase 0F: advect storminess so storms travel with wind
            float adv_storm =
                sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::storminess);
            cell.storminess = cell.storminess * 0.5f + adv_storm * 0.5f;
            // Phase 1: advect precip_budget (air masses carry capacity)
            cell.precip_budget =
                sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::precip_budget);
            // Partially advect wind momentum
            float adv_u = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::u);
            float adv_v = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::v);
            cell.u = cell.u * 0.4f + adv_u * 0.6f;
            cell.v = cell.v * 0.4f + adv_v * 0.6f;

            // Phase 3: Upper layer advection (faster speed)
            float trace_xu = src.u_upper * dt_days * UPPER_ADVECT_SPEED;
            float trace_yu = src.v_upper * dt_days * UPPER_ADVECT_SPEED;
            float trace_dist_u = std::sqrt(trace_xu * trace_xu + trace_yu * trace_yu);
            if (trace_dist_u > MAX_TRACE_DIST * 1.5f) {
                float su = MAX_TRACE_DIST * 1.5f / trace_dist_u;
                trace_xu *= su;
                trace_yu *= su;
            }
            if (diag)
                displacements_upper.push_back(trace_dist_u);
            float x0u = wrap_x(static_cast<float>(ax) - trace_xu);
            float y0u = static_cast<float>(ay) - trace_yu;
            cell.T_upper = sample_field(atmo.scratch, aw, ah, x0u, y0u, &AtmosphereCell::T_upper);
            cell.q_upper = sample_field(atmo.scratch, aw, ah, x0u, y0u, &AtmosphereCell::q_upper);
            cell.u_upper =
                sample_field(atmo.scratch, aw, ah, x0u, y0u, &AtmosphereCell::u_upper) * 0.7f +
                cell.u_upper * 0.3f;
            cell.v_upper =
                sample_field(atmo.scratch, aw, ah, x0u, y0u, &AtmosphereCell::v_upper) * 0.7f +
                cell.v_upper * 0.3f;
        }
    }

    // ── Variance snapshot: after advection ──────────────────────────────
    if (diag) {
        diag->T_anom_var[1] = field_variance(atmo.cells, &AtmosphereCell::T_anom);
        diag->q_anom_var[1] = field_variance(atmo.cells, &AtmosphereCell::q_anom);
        diag->p_anom_var[1] = field_variance(atmo.cells, &AtmosphereCell::p_anom);
        // Displacement percentiles
        std::sort(displacements.begin(), displacements.end());
        std::sort(displacements_upper.begin(), displacements_upper.end());
        auto pctf = [](std::vector<float>& v, float p) -> float {
            if (v.empty())
                return 0.0f;
            size_t idx = static_cast<size_t>(p * static_cast<float>(v.size() - 1));
            return v[std::min(idx, v.size() - 1)];
        };
        diag->displacement_p50 = pctf(displacements, 0.5f);
        diag->displacement_p90 = pctf(displacements, 0.9f);
        diag->displacement_max = displacements.empty() ? 0.0f : displacements.back();
        diag->displacement_upper_p50 = pctf(displacements_upper, 0.5f);
        diag->displacement_upper_p90 = pctf(displacements_upper, 0.9f);
        uint32_t sub_tenth = 0;
        for (float d : displacements)
            if (d < 0.1f)
                sub_tenth++;
        diag->frac_sub_tenth =
            static_cast<float>(sub_tenth) / static_cast<float>(displacements.size());
    }

    // ── Invariant 2: Correct advection-induced water drift ────────────
    // Reconstruct composite q for water tracking, then correct anomalies
    {
        // Reconstruct q from base + anom for water accounting
        for (auto& c : atmo.cells)
            c.q = std::clamp(c.q_base + c.q_anom, 0.0f, 1.0f);

        float water_after_advect = 0.0f;
        for (const auto& c : atmo.cells)
            water_after_advect += c.q + c.cloud;

        float advect_drift = water_after_advect - water_before_advect;
        float drift_frac =
            (water_before_advect > 0.01f) ? advect_drift / water_before_advect : 0.0f;
        atmo.last_water_correction = 0.0f;
        if (std::abs(drift_frac) > 0.001f && water_after_advect > 0.01f) {
            float correction = -advect_drift / water_after_advect;
            for (auto& c : atmo.cells) {
                float old_q = c.q;
                c.q += c.q * correction;
                c.q = std::clamp(c.q, 0.0f, 1.0f);
                float delta_q = c.q - old_q;
                c.q_anom += delta_q;  // transient correction on weather field, not climate baseline
                c.cloud += c.cloud * correction;
                c.cloud = std::clamp(c.cloud, 0.0f, 1.0f);
            }
            atmo.last_water_correction = correction;
        }
    }

    // ── Step 4b: Moisture anomaly diffusion ─────────────────────────────
    // T_anom smoothing handled by stochastic eddy persistence decay (no explicit diffusion needed).
    // Moisture anomalies still need Laplacian diffusion for smooth transport.
    atmo.scratch = atmo.cells;
    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            uint32_t ax_l = (ax == 0) ? aw - 1 : ax - 1;
            uint32_t ax_r = (ax == aw - 1) ? 0 : ax + 1;
            uint32_t ay_u = (ay == 0) ? 0 : ay - 1;
            uint32_t ay_d = (ay == ah - 1) ? ah - 1 : ay + 1;

            const auto& c = atmo.scratch[ay * aw + ax];
            const auto& left = atmo.scratch[ay * aw + ax_l];
            const auto& right = atmo.scratch[ay * aw + ax_r];
            const auto& up = atmo.scratch[ay_u * aw + ax];
            const auto& down = atmo.scratch[ay_d * aw + ax];

            float lap_q_anom =
                left.q_anom + right.q_anom + up.q_anom + down.q_anom - 4.0f * c.q_anom;

            auto& cell = atmo.cell_at(ax, ay);
            cell.q_anom += K_MOIST_DIFFUSE * lap_q_anom * dt_days;
        }
    }

    // ── Variance snapshot: after diffusion ──────────────────────────────
    if (diag) {
        diag->T_anom_var[2] = field_variance(atmo.cells, &AtmosphereCell::T_anom);
        diag->q_anom_var[2] = field_variance(atmo.cells, &AtmosphereCell::q_anom);
        diag->p_anom_var[2] = field_variance(atmo.cells, &AtmosphereCell::p_anom);
    }

    // ── Step 4c: Vertical exchange (Phase 3) ──────────────────────────
    // Heat and moisture exchange between lower and upper atmosphere.
    // Unstable conditions (warm surface, cold aloft) enhance exchange.
    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);
            float T_lower = cell.T_base + cell.T_anom;

            // Stability diagnostic
            cell.stability = cell.T_upper - T_lower;

            // Upper layer anomaly: deviation from expected lapse rate
            float T_upper_ref = cell.T_base - 25.0f;
            float T_upper_anom = cell.T_upper - T_upper_ref;

            // Excess instability beyond normal lapse rate
            float excess_instab = std::max(0.0f, EXPECTED_LAPSE - cell.stability);

            // Vertical heat exchange based on ANOMALY mismatch, not full T difference
            // At equilibrium (T_anom=0, T_upper_anom=0), heat_flux=0 — no spurious drain
            float heat_flux = K_VERT_HEAT * (cell.T_anom - T_upper_anom) * dt_days;

            // Convective boost only when lapse rate exceeds adiabatic (truly unstable)
            if (excess_instab > 2.0f) {
                float instab_factor = std::min((excess_instab - 2.0f) * 0.1f, 1.0f);
                heat_flux *= (1.0f + CONVECTIVE_VERT_BOOST * instab_factor);
            }
            cell.T_upper += heat_flux;
            cell.T_anom -= heat_flux;

            // Moisture flux upward only when truly unstable (steeper than normal lapse)
            if (excess_instab > 0.0f) {
                float q_lower = std::clamp(cell.q_base + cell.q_anom, 0.0f, 1.0f);
                float moist_flux =
                    K_VERT_MOIST * q_lower * std::min(excess_instab * 0.05f, 1.0f) * dt_days;
                moist_flux = std::min(moist_flux, q_lower * 0.2f);
                cell.q_upper += moist_flux;
                cell.q_anom -= moist_flux;
            }

            // Orographic forced uplift: mountains push lower moisture to upper
            if (cell.avg_slope > 0.1f && cell.avg_elevation > 0.3f) {
                float wind_spd = std::sqrt(cell.u * cell.u + cell.v * cell.v);
                float forced_uplift = cell.avg_slope * wind_spd * 0.02f * dt_days;
                float q_lower = std::clamp(cell.q_base + cell.q_anom, 0.0f, 1.0f);
                float transfer = std::min(forced_uplift, q_lower * 0.1f);
                cell.q_upper += transfer;
                cell.q_anom -= transfer;
            }

            // Upper layer radiative cooling (OLR — upper layer radiates to space)
            // Same Stefan-Boltzmann T^4 physics as lower layer, but at reduced rate
            // because the upper troposphere has lower optical depth (less greenhouse absorption)
            // CRITICAL: equilibrium uses T_base (not T_lower) to prevent cooling feedback
            // when T_anom goes negative — anchors upper layer to climate, not weather state
            {
                static constexpr float K_RAD_UPPER = 0.015f;
                float T_up_K = std::max(cell.T_upper + 273.15f, 200.0f);
                // Reference: lapse-rate equilibrium anchored to climate baseline
                float T_up_eq = cell.T_base - 25.0f;
                float T_up_eq_K = std::max(T_up_eq + 273.15f, 200.0f);
                float sigma_up = K_RAD_UPPER / (4.0f * T_up_eq_K * T_up_eq_K * T_up_eq_K);
                float emit_up = T_up_K * T_up_K * T_up_K * T_up_K;
                float absorb_up = T_up_eq_K * T_up_eq_K * T_up_eq_K * T_up_eq_K;
                cell.T_upper -= sigma_up * (emit_up - absorb_up) * dt_days;
            }

            // Upper layer relaxation toward lapse-rate reference
            // Strong relaxation keeps upper layer near climate equilibrium
            // while allowing weather anomalies to modulate it
            {
                static constexpr float K_UPPER_RELAX = 0.5f;  // per day (strong anchor)
                float T_up_ref = cell.T_base - 25.0f;
                cell.T_upper += K_UPPER_RELAX * (T_up_ref - cell.T_upper) * dt_days;
            }

            // Clamp upper values
            cell.T_upper = std::clamp(cell.T_upper, -60.0f, 20.0f);
            cell.q_upper = std::clamp(cell.q_upper, 0.0f, 1.0f);

            // Upper pressure from temperature
            cell.p_upper = 500.0f - 0.5f * (cell.T_upper + 10.0f);
        }
    }

    // ── Variance snapshot: after vertical exchange ──────────────────────
    if (diag) {
        diag->T_anom_var[3] = field_variance(atmo.cells, &AtmosphereCell::T_anom);
        diag->q_anom_var[3] = field_variance(atmo.cells, &AtmosphereCell::q_anom);
        diag->p_anom_var[3] = field_variance(atmo.cells, &AtmosphereCell::p_anom);
    }

    // ── Step 5: Solar forcing + ground temp + air temp ─────────────────
    // CRITICAL: This MUST happen AFTER advection. Advection transports air
    // masses (moving warm/cold fronts). Temperature forcing then acts on the
    // transported air in-place. If forcing ran before advection, advection
    // would overwrite the forced temperatures with stale upwind values,
    // causing systematic cooling (especially at the equator).

    for (uint32_t ay = 0; ay < ah; ++ay) {
        // Latitude: y=0 is north pole, y=height is south pole
        float lat_frac = static_cast<float>(ay) / static_cast<float>(ah);
        float latitude = (0.5f - lat_frac) * 2.0f;  // +1 north, -1 south

        // Solar intensity: stronger at equator, modulated by season + time of day
        float solar_base = std::max(0.0f, 1.0f - latitude * latitude * 0.8f);
        float solar_seasonal = solar_base + latitude * season_offset * 0.35f;
        float solar = solar_seasonal * (0.65f + 0.35f * std::max(0.0f, diurnal));

        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);

            // Albedo: snow reflects, water absorbs differently
            float albedo = 0.3f;
            if (cell.avg_snow > 0.1f)
                albedo += std::min(cell.avg_snow, 1.0f) * 0.35f;
            if (cell.is_water)
                albedo = 0.06f + 0.1f * (1.0f - std::max(0.0f, diurnal));
            albedo = std::clamp(albedo, 0.05f, 0.85f);

            // Cloud shading
            float cloud_shade = 1.0f - cell.cloud * 0.55f;

            // Reference temperature from latitude + elevation
            // Base range: equator ~33°C peak, poles ~ -15°C
            float T_ref = -15.0f + solar_seasonal * 48.0f;
            T_ref -= cell.avg_elevation * LAPSE_PER_ELEV;

            // Phase 0B: Aspect-based solar heating
            // South-facing slopes (NH) receive more sunlight, north-facing less
            float hemisphere_sign = -std::clamp(latitude * 4.0f, -1.0f, 1.0f);  // smooth transition
            float aspect_solar =
                cell.avg_slope * std::cos(cell.avg_aspect) * hemisphere_sign * 0.5f;

            // Phase 0C: Maritime moderation from dist_ocean
            // Coastal cells have higher thermal inertia (smaller T swings)
            float maritime = 1.0f / (1.0f + cell.avg_dist_ocean * 0.02f);

            // Evaporative cooling from soil moisture (stronger over water)
            float evap_cool = cell.avg_soil_wet * K_EVAP_COOL * (cell.is_water ? 1.8f : 1.0f);

            // Wind speed for convective effects
            float wind_speed = std::sqrt(cell.u * cell.u + cell.v * cell.v);

            // Ground temperature: energy balance
            float convective = K_CONVECTIVE * wind_speed;

            // Invariant 1: Stefan-Boltzmann radiative cooling (T^4 law)
            float T_K = std::max(cell.ground_temp + 273.15f, 200.0f);
            float T_ref_K = std::max(T_ref + 273.15f, 200.0f);
            float sigma_norm = K_RAD / (4.0f * T_ref_K * T_ref_K * T_ref_K);
            float emit = T_K * T_K * T_K * T_K;
            float absorb = T_ref_K * T_ref_K * T_ref_K * T_ref_K;
            float radiative_cooling = sigma_norm * (emit - absorb);

            // Ocean thermal inertia: scale down heating/cooling by factor of ~5
            // (mixed layer heat capacity >> land surface heat capacity)
            float thermal_inertia = cell.is_water ? 0.2f : 1.0f;
            cell.ground_temp +=
                dt_days * thermal_inertia *
                (K_SOLAR * solar * cloud_shade * (1.0f - albedo) +
                 K_ASPECT_SOLAR * aspect_solar * solar - radiative_cooling - 0.03f * evap_cool +
                 (K_AIR_GROUND + convective) * (cell.T - cell.ground_temp));

            // Phase 0C: Maritime moderation — coastal cells relax toward T_ref
            cell.ground_temp += maritime * 0.03f * (T_ref - cell.ground_temp) * dt_days;

            // Water bodies: strong relaxation toward T_ref (high thermal inertia)
            if (cell.is_water) {
                float alpha_ocean = 1.0f - std::exp(-dt_days / 15.0f);  // tau=15 days
                cell.ground_temp = cell.ground_temp * (1.0f - alpha_ocean) + T_ref * alpha_ocean;
            }

            // Ground-air coupling: adds to T_anom for local heating/cooling
            // QG thermal wind sets the large-scale T_anom pattern (step 2g),
            // ground coupling adds local surface effects on top
            float T_composite = cell.T_base + cell.T_anom;
            cell.T_anom += dt_days * K_GROUND_AIR * 0.5f * (cell.ground_temp - T_composite);

            // Anomaly relaxation: prevents ground-coupling drift from accumulating
            cell.T_anom *= (1.0f - K_ANOM_RELAX_T * dt_days);
        }
    }

    // ── Variance snapshot: after solar forcing ──────────────────────────
    if (diag) {
        diag->T_anom_var[4] = field_variance(atmo.cells, &AtmosphereCell::T_anom);
        diag->q_anom_var[4] = field_variance(atmo.cells, &AtmosphereCell::q_anom);
        diag->p_anom_var[4] = field_variance(atmo.cells, &AtmosphereCell::p_anom);
    }

    // ── Step 6: Moisture sources + condensation + precipitation ──────

    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);

            // Reconstruct composite T and q for this cell's physics
            cell.T = cell.T_base + cell.T_anom;
            cell.q = std::clamp(cell.q_base + cell.q_anom, 0.0f, 1.0f);

            // Wind speed for evaporation acceleration
            float wind_spd = std::sqrt(cell.u * cell.u + cell.v * cell.v);
            float wind_evap_factor = 1.0f + wind_spd * 0.6f;

            // Saturation humidity (Clausius-Clapeyron approx)
            float q_sat = std::clamp(0.05f * std::exp(0.06f * cell.T), 0.02f, 1.0f);

            // Humidity deficit
            float humidity_deficit = std::max(0.0f, 1.0f - cell.q / q_sat);

            if (cell.is_water) {
                float temp_factor = std::clamp(cell.T / 25.0f, 0.2f, 1.5f);
                // Cloud cover reduces solar heating of ocean surface → less evaporation
                float cloud_shading = 1.0f - cell.cloud * 0.8f;
                cloud_shading = std::clamp(cloud_shading, 0.2f, 1.0f);
                float evap = OCEAN_EVAP_RATE * temp_factor * wind_evap_factor * humidity_deficit *
                             cloud_shading * dt_days;
                cell.q_anom += evap;
            } else {
                // Land evaporation: soil+water dependence with floor for minimum recycling
                // Floor of 0.15 ensures even dry regions contribute some moisture
                // Saturated land (soil+sw > 0.5) gets bonus evap (wetlands, flooded areas)
                // Coastal islands get a convergence/roughness bonus (1.15x within 10 tiles)
                float wet = std::clamp(cell.avg_soil_wet, 0.0f, 1.0f);
                float saturated_bonus = std::clamp(cell.avg_soil_wet - 0.5f, 0.0f, 1.0f);
                float coastal_boost = (cell.avg_dist_ocean < 10.0f)
                    ? 1.0f + 0.15f * (1.0f - cell.avg_dist_ocean / 10.0f) : 1.0f;
                float soil_source = ((0.15f + 0.85f * wet * wet) * LAND_EVAP_RATE
                                    + saturated_bonus * 0.5f * LAND_EVAP_RATE) * coastal_boost;
                float temp_factor = std::clamp(cell.T / 25.0f, 0.1f, 1.2f);
                float evap_amount =
                    soil_source * temp_factor * wind_evap_factor * humidity_deficit * dt_days;
                cell.q_anom += evap_amount;
            }

            // Update composite q after evaporation
            cell.q = std::clamp(cell.q_base + cell.q_anom, 0.0f, 1.0f);

            // Orographic uplift: wind hitting rising terrain forces condensation
            // Phase 0A: Use per-cell slope+aspect for finer wind·slope coupling
            float uplift = 0.0f;
            // Coarse elevation gradient (original approach)
            if (ax > 0 && ax < aw - 1) {
                float elev_grad =
                    atmo.cell_at(ax + 1, ay).avg_elevation - atmo.cell_at(ax - 1, ay).avg_elevation;
                uplift += std::max(0.0f, cell.u * elev_grad);
            }
            if (ay > 0 && ay < ah - 1) {
                float elev_grad =
                    atmo.cell_at(ax, ay + 1).avg_elevation - atmo.cell_at(ax, ay - 1).avg_elevation;
                uplift += std::max(0.0f, cell.v * elev_grad);
            }
            // Fine wind·slope uplift from averaged terrain slope and aspect
            float slope_uplift =
                std::max(0.0f, cell.u * cell.avg_slope * std::cos(cell.avg_aspect) +
                                   cell.v * cell.avg_slope * std::sin(cell.avg_aspect));
            uplift += slope_uplift * 0.5f;
            uplift = std::clamp(uplift * UPLIFT_STRENGTH, 0.0f, 0.4f);

            // Convergence-driven lifting (frontogenesis): where wind converges,
            // air is forced upward, creating condensation and self-reinforcing storms.
            // This is the key mechanism for generating organized weather systems.
            float convergence_lift = 0.0f;
            {
                float dudx = 0.0f, dvdy = 0.0f;
                if (ax > 0 && ax < aw - 1)
                    dudx = (atmo.cell_at(ax + 1, ay).u - atmo.cell_at(ax - 1, ay).u) * 0.5f;
                if (ay > 0 && ay < ah - 1)
                    dvdy = (atmo.cell_at(ax, ay + 1).v - atmo.cell_at(ax, ay - 1).v) * 0.5f;
                float conv = std::max(0.0f, -(dudx + dvdy));  // convergence > 0
                float rh = (q_sat > 0.01f) ? cell.q / q_sat : 0.0f;
                // Only trigger when air is already moderately humid (RH > 0.6)
                float rh_factor = std::clamp((rh - 0.6f) / 0.4f, 0.0f, 1.0f);
                convergence_lift = conv * rh_factor * 0.5f;  // LIFT_K = 0.5 (moderate)
                float conv_cond = convergence_lift * cell.q * 0.15f * dt_days;
                conv_cond = std::min(conv_cond, cell.q * 0.1f);
                if (conv_cond > 0.0005f) {
                    cell.cloud += conv_cond;
                    cell.q_anom -= conv_cond;
                    cell.q -= conv_cond;
                    cell.T_anom += conv_cond * K_LATENT * 0.5f;  // reduced latent heating
                    cell.T = cell.T_base + cell.T_anom;
                    // Latent heating deepens the low (strengthens convergence feedback)
                    cell.p_anom -= conv_cond * 1.5f;
                }
            }

            // Uplift reduces effective saturation (forced condensation at altitude)
            float effective_qsat = q_sat - (uplift + convergence_lift * 0.1f) * 0.3f;
            effective_qsat = std::max(effective_qsat, 0.05f);

            // Condensation: excess humidity → clouds + latent heat release
            // Noisy condensation threshold creates patchy cloud clusters instead of
            // uniform cloud layers. Noise varies smoothly in space (~6 cell clusters)
            // and evolves every ~3 hours, creating persistent cloud systems.
            // Temporal crossfade: blend between two timesteps to prevent global
            // pattern teleport at integer boundaries.
            float cond_phase = atmo.day_of_year * 8.0f;  // ~3h per unit
            int cond_t0 = static_cast<int>(std::floor(cond_phase));
            int cond_t1 = cond_t0 + 1;
            float cond_blend = cond_phase - static_cast<float>(cond_t0);
            // Smooth the blend with cubic Hermite to avoid linear ramp artifacts
            cond_blend = cond_blend * cond_blend * (3.0f - 2.0f * cond_blend);
            float fax = static_cast<float>(ax);
            float fay = static_cast<float>(ay);
            // Second noise layer uses spatial offset (+1000) to decorrelate from first
            float n0_a = smooth_noise(fax, fay, cond_t0, 6);
            float n1_a = smooth_noise(fax, fay, cond_t1, 6);
            float n0_b = smooth_noise(fax + 1000.0f, fay + 1000.0f, cond_t0 + 41, 3);
            float n1_b = smooth_noise(fax + 1000.0f, fay + 1000.0f, cond_t1 + 41, 3);
            float cond_noise = (n0_a + (n1_a - n0_a) * cond_blend) * 0.65f +
                               (n0_b + (n1_b - n0_b) * cond_blend) * 0.35f;
            // Noise biases q_sat by ±25%: some cells condense earlier, some later
            // High pressure raises effective q_sat (subsidence warms air, inhibits condensation)
            float p_anom = std::max(0.0f, (cell.p - 1013.0f) * 0.05f);
            float noisy_qsat = effective_qsat * (0.75f + cond_noise * 0.50f + p_anom);
            noisy_qsat = std::max(noisy_qsat, 0.02f);

            if (cell.q > noisy_qsat) {
                float excess = cell.q - noisy_qsat;
                float condensed = excess * 0.5f;  // gradual condensation
                cell.cloud += condensed;
                cell.q_anom -= condensed;
                cell.q -= condensed;
                // Latent heat: condensation warms the air
                cell.T_anom += condensed * K_LATENT;
                cell.T = cell.T_base + cell.T_anom;  // keep composite consistent
            }

            // Phase 3: Upper layer condensation (additional cloud source)
            {
                float q_sat_upper = std::clamp(0.05f * std::exp(0.06f * cell.T_upper), 0.02f, 1.0f);
                if (cell.q_upper > q_sat_upper) {
                    float excess = cell.q_upper - q_sat_upper;
                    float condensed = excess * 0.5f;
                    cell.cloud += condensed;
                    cell.q_upper -= condensed;
                    cell.T_upper += condensed * K_LATENT;
                }
                cell.q_upper = std::clamp(cell.q_upper, 0.0f, 1.0f);
            }

            // Phase 1: Budget recharge — ocean recharges, land barely recharges
            if (cell.is_water) {
                cell.precip_budget += BUDGET_OCEAN_RECHARGE * dt_days;
            } else {
                cell.precip_budget += BUDGET_LAND_RECHARGE * dt_days;
            }
            // Atmospheric mixing slowly restores budget toward 1.0
            cell.precip_budget += BUDGET_MIXING * (1.0f - cell.precip_budget) * dt_days;

            // Precipitation: threshold-based — clouds must thicken before rain
            // This creates a natural cloud lifecycle:
            //   build (condensation) → thicken → rain (drain) → thin → dissipate
            // Cloud advection handles persistence and wind-driven movement.
            // Thin cloud (< threshold) drifts without raining; thick cloud rains.
            float precip = 0.0f;
            if (cell.cloud > CLOUD_PRECIP_THRESHOLD) {
                // Rain rate scales with excess above threshold — heavier cloud = heavier rain
                float excess = cell.cloud - CLOUD_PRECIP_THRESHOLD;
                precip = excess * CLOUD_PRECIP_RATE * dt_days;
                precip = std::min(precip, cell.cloud - CLOUD_PRECIP_THRESHOLD * 0.5f);
            }
            // Orographic precipitation: forced uplift always squeezes thick cloud
            if (uplift > 0.02f && cell.cloud > CLOUD_PRECIP_THRESHOLD * 0.5f) {
                float oro_precip = cell.cloud * uplift * 0.4f * dt_days;
                oro_precip = std::min(oro_precip, cell.cloud - precip);
                oro_precip = std::max(oro_precip, 0.0f);
                precip += oro_precip;
            }
            precip = std::max(precip, 0.0f);

            // Phase 1: Scale rain efficiency by remaining budget
            float budget_eff = std::clamp(cell.precip_budget, 0.0f, 1.0f);
            precip *= budget_eff;
            // Deplete budget when it rains
            cell.precip_budget -= precip * BUDGET_RAIN_COST;

            cell.cloud -= precip;
            cell.precip_rate = std::clamp(precip / (dt_days + 0.001f), 0.0f, 1.0f);

            // Cloud dissipation: two pathways
            // 1. Evaporative: cloud → vapor (limited by saturation headroom)
            // 2. Radiative: longwave emission destroys cloud without adding moisture
            //    This prevents perpetual cloud over warm saturated ocean.
            {
                float deficit = std::max(0.0f, 1.0f - cell.q / std::max(q_sat, 0.01f));
                float p_anomaly = std::clamp((cell.p - 1013.0f) * 0.1f, -0.5f, 1.0f);
                float subsidence = std::max(0.0f, p_anomaly) * CLOUD_SUBSIDE_RATE;
                float dissip_rate = CLOUD_EVAP_BASE + deficit * CLOUD_EVAP_DRY + subsidence;
                float total_dissip = cell.cloud * dissip_rate * dt_days;
                // Evaporative pathway: return to vapor if below saturation
                float can_absorb = std::max(0.0f, q_sat - cell.q);
                float evap_dissip = std::min(total_dissip, can_absorb);
                // Radiative pathway: remaining dissipation lost to longwave (no vapor return)
                static constexpr float RADIATIVE_DISSIP_FRAC = 0.7f;
                float radiative_dissip = (total_dissip - evap_dissip) * RADIATIVE_DISSIP_FRAC;
                cell.cloud -= evap_dissip + radiative_dissip;
                cell.cloud = std::max(cell.cloud, 0.0f);
                cell.q_anom += evap_dissip;
                cell.q += evap_dissip;
            }

            // Hard saturation enforcement: any remaining q > q_sat → cloud
            // This catches supersaturation from advection, diffusion, evaporation
            if (cell.q > q_sat) {
                float excess = cell.q - q_sat;
                cell.cloud += excess;
                cell.q_anom -= excess;
                cell.q = q_sat;
                cell.T_anom += excess * K_LATENT * 0.5f;  // reduced latent heating
                cell.T = cell.T_base + cell.T_anom;
            }

            // Rain shadow: remove moisture on leeward side of mountains
            if (uplift > 0.05f) {
                float rain_shadow_loss =
                    std::min(cell.q * uplift * 0.2f * dt_days, std::max(cell.q, 0.0f));
                cell.q_anom -= rain_shadow_loss;
                cell.q -= rain_shadow_loss;
            }

            // ── Invariant 7: Convective instability release ────────────
            float ground_air_diff = cell.ground_temp - cell.T;
            float rh_conv = (q_sat > 0.01f) ? cell.q / q_sat : 0.0f;
            if (ground_air_diff > CONVECTIVE_THRESHOLD && rh_conv > 0.7f) {
                float instab = (ground_air_diff - CONVECTIVE_THRESHOLD) * 0.1f;
                instab = std::min(instab, 0.5f);

                float convective_condensed = cell.q * instab * dt_days;
                convective_condensed = std::min(convective_condensed, cell.q * 0.3f);
                cell.cloud += convective_condensed;
                cell.q_anom -= convective_condensed;
                cell.q -= convective_condensed;
                cell.T_anom += convective_condensed * K_LATENT;
                cell.T = cell.T_base + cell.T_anom;

                float conv_precip = cell.cloud * CONVECTIVE_PRECIP * instab * dt_days;
                conv_precip = std::min(conv_precip, cell.cloud);
                cell.cloud -= conv_precip;
                cell.precip_rate =
                    std::min(cell.precip_rate + conv_precip / (dt_days + 0.001f), 1.0f);
            }

            // ── Storm formation ────────────────────────────────────────
            // Storms = convergence + instability + moisture
            // Wind convergence: surrounding wind flowing inward
            float convergence = 0.0f;
            if (ax > 0 && ax < aw - 1) {
                float du = atmo.cell_at(ax + 1, ay).u - atmo.cell_at(ax - 1, ay).u;
                convergence -= du * 0.5f;  // negative du = converging
            }
            if (ay > 0 && ay < ah - 1) {
                float dv = atmo.cell_at(ax, ay + 1).v - atmo.cell_at(ax, ay - 1).v;
                convergence -= dv * 0.5f;
            }
            convergence = std::max(0.0f, convergence);

            // Temperature gradient magnitude (instability)
            float T_grad = 0.0f;
            if (ax > 0 && ax < aw - 1) {
                float dTx = atmo.cell_at(ax + 1, ay).T - atmo.cell_at(ax - 1, ay).T;
                T_grad += dTx * dTx;
            }
            if (ay > 0 && ay < ah - 1) {
                float dTy = atmo.cell_at(ax, ay + 1).T - atmo.cell_at(ax, ay - 1).T;
                T_grad += dTy * dTy;
            }
            T_grad = std::sqrt(T_grad);

            // Storm intensity: Phase 3 uses actual vertical instability
            float humidity_ratio = (q_sat > 0.01f) ? cell.q / q_sat : 0.0f;
            float conv_excess = std::max(0.0f, convergence - 0.2f);
            // Phase 3: vertical instability relative to expected lapse rate
            float storm_vert_instab = std::max(0.0f, EXPECTED_LAPSE - cell.stability);
            float instab_combined =
                std::min(conv_excess * 1.5f + storm_vert_instab * 0.05f + T_grad * 0.03f, 1.0f);
            float boosted = instab_combined * (1.0f + cell.cloud * 0.5f);
            float storm_raw = humidity_ratio * boosted * boosted * boosted;
            float computed_storm = std::clamp(storm_raw, 0.0f, 1.0f);
            cell.storminess = cell.storminess * 0.5f + computed_storm * 0.5f;

            // Phase 2: anomaly relaxation for q
            cell.q_anom *= (1.0f - K_ANOM_RELAX_Q * dt_days);
            cell.p_anom *= (1.0f - K_ANOM_RELAX_P * dt_days);

            // Phase 2: composite update — all external code reads these
            cell.T = cell.T_base + cell.T_anom;
            cell.q = std::clamp(cell.q_base + cell.q_anom, 0.0f, 1.0f);
            cell.p = cell.p_base + cell.p_anom;

            // Clamp remaining fields
            cell.cloud = std::clamp(cell.cloud, 0.0f, 1.0f);
            cell.precip_budget = std::clamp(cell.precip_budget, 0.0f, 2.0f);
        }
    }

    // ── Variance snapshot: after moisture step (end of tick) ────────────
    if (diag) {
        diag->T_anom_var[5] = field_variance(atmo.cells, &AtmosphereCell::T_anom);
        diag->q_anom_var[5] = field_variance(atmo.cells, &AtmosphereCell::q_anom);
        diag->p_anom_var[5] = field_variance(atmo.cells, &AtmosphereCell::p_anom);
        diag->T_anom_var[6] = diag->T_anom_var[5];  // alias end = after_moisture
        diag->q_anom_var[6] = diag->q_anom_var[5];
        diag->p_anom_var[6] = diag->p_anom_var[5];
    }

    // Track total atmospheric water for diagnostics
    float water_total = 0.0f;
    for (const auto& c : atmo.cells)
        water_total += c.q + c.cloud;
    atmo.total_water = water_total;

    // Phase 4: track total energy (diagnostic only — no correction)
    // Ground coupling acts as an external energy reservoir, so atmospheric energy
    // is not expected to be conserved. Energy drift is tracked for monitoring only.
    atmo.total_energy = compute_total_energy(atmo.cells);
}

// ── Atmosphere statistics ─────────────────────────────────────────────

AtmosphereStats compute_atmosphere_stats(const AtmosphereState& atmo) {
    AtmosphereStats s;
    if (atmo.cells.empty())
        return s;

    s.T_min = 1e9f;
    s.T_max = -1e9f;
    s.q_min = 1e9f;
    s.q_max = -1e9f;
    s.cloud_min = 1e9f;
    s.cloud_max = -1e9f;
    s.precip_min = 1e9f;
    s.precip_max = -1e9f;
    s.wind_min = 1e9f;
    s.wind_max = -1e9f;
    s.p_min = 1e9f;
    s.p_max = -1e9f;
    s.budget_min = 1e9f;
    s.budget_max = -1e9f;

    double T_sum = 0, q_sum = 0, cloud_sum = 0, precip_sum = 0, wind_sum = 0, storm_sum = 0;
    double T_sum2 = 0, wind_sum2 = 0, q_sum2 = 0, precip_sum2 = 0;
    double budget_sum = 0;
    double T_anom_sum2 = 0, q_anom_sum2 = 0;
    s.T_anom_min = 1e9f;
    s.T_anom_max = -1e9f;
    double T_upper_sum = 0, wind_upper_sum = 0, stability_sum = 0;
    s.wind_upper_max = -1e9f;
    s.stability_min = 1e9f;
    s.stability_max = -1e9f;
    double wind_u_sum = 0, wind_v_sum = 0;  // for direction spread
    double band_T[3] = {}, band_wind[3] = {}, band_q[3] = {};
    uint32_t band_count[3] = {};
    size_t n = atmo.cells.size();

    for (size_t ci = 0; ci < n; ++ci) {
        const auto& c = atmo.cells[ci];
        int band = static_cast<int>((ci / atmo.width) * 3 / atmo.height);
        band = std::clamp(band, 0, 2);
        s.T_min = std::min(s.T_min, c.T);
        s.T_max = std::max(s.T_max, c.T);
        T_sum += c.T;
        T_sum2 += c.T * c.T;

        s.q_min = std::min(s.q_min, c.q);
        s.q_max = std::max(s.q_max, c.q);
        q_sum += c.q;
        q_sum2 += c.q * c.q;

        s.cloud_min = std::min(s.cloud_min, c.cloud);
        s.cloud_max = std::max(s.cloud_max, c.cloud);
        cloud_sum += c.cloud;

        s.precip_min = std::min(s.precip_min, c.precip_rate);
        s.precip_max = std::max(s.precip_max, c.precip_rate);
        precip_sum += c.precip_rate;

        precip_sum2 += c.precip_rate * c.precip_rate;

        float spd = std::sqrt(c.u * c.u + c.v * c.v);
        s.wind_min = std::min(s.wind_min, spd);
        s.wind_max = std::max(s.wind_max, spd);
        wind_sum += spd;
        wind_sum2 += spd * spd;
        // Accumulate unit wind vectors for direction spread (mean resultant length)
        if (spd > 0.01f) {
            wind_u_sum += c.u / spd;
            wind_v_sum += c.v / spd;
        }

        s.p_min = std::min(s.p_min, c.p);
        s.p_max = std::max(s.p_max, c.p);

        storm_sum += c.storminess;
        s.storm_max = std::max(s.storm_max, c.storminess);
        s.precip_max_cell = std::max(s.precip_max_cell, c.precip_rate);
        s.budget_min = std::min(s.budget_min, c.precip_budget);
        s.budget_max = std::max(s.budget_max, c.precip_budget);
        budget_sum += c.precip_budget;
        s.T_anom_min = std::min(s.T_anom_min, c.T_anom);
        s.T_anom_max = std::max(s.T_anom_max, c.T_anom);
        T_anom_sum2 += static_cast<double>(c.T_anom) * c.T_anom;
        q_anom_sum2 += static_cast<double>(c.q_anom) * c.q_anom;
        T_upper_sum += c.T_upper;
        float spd_upper = std::sqrt(c.u_upper * c.u_upper + c.v_upper * c.v_upper);
        wind_upper_sum += spd_upper;
        s.wind_upper_max = std::max(s.wind_upper_max, spd_upper);
        s.stability_min = std::min(s.stability_min, c.stability);
        s.stability_max = std::max(s.stability_max, c.stability);
        stability_sum += c.stability;
        if (c.stability > INVERSION_THRESHOLD)
            ++s.inversion_cells;

        if (c.precip_rate > 0.01f)
            ++s.raining_cells;
        if (c.cloud > 0.05f)
            ++s.cloudy_cells;
        if (c.storminess > 0.5f)
            ++s.severe_storm_cells;
        if (c.precip_rate > 0.15f)
            ++s.heavy_rain_cells;
        if (c.T > 30.0f)
            ++s.heat_wave_cells;
        if (c.T < -25.0f)
            ++s.deep_freeze_cells;
        if (spd > 4.0f)
            ++s.gale_cells;

        // Latitude-band accumulation (folded in to avoid second grid pass)
        band_T[band] += c.T;
        band_q[band] += c.q;
        band_wind[band] += spd;  // reuse already-computed wind speed
        ++band_count[band];
    }

    auto fn = static_cast<float>(n);
    s.T_mean = static_cast<float>(T_sum / fn);
    s.q_mean = static_cast<float>(q_sum / fn);
    s.cloud_mean = static_cast<float>(cloud_sum / fn);
    s.precip_mean = static_cast<float>(precip_sum / fn);
    s.wind_mean = static_cast<float>(wind_sum / fn);
    s.storm_mean = static_cast<float>(storm_sum / fn);
    s.budget_mean = static_cast<float>(budget_sum / fn);
    // Phase 2: anomaly stddev (mean is ~0 by design)
    s.T_anom_stddev = static_cast<float>(std::sqrt(T_anom_sum2 / static_cast<double>(fn)));
    s.q_anom_stddev = static_cast<float>(std::sqrt(q_anom_sum2 / static_cast<double>(fn)));
    // Phase 3: upper layer stats
    s.T_upper_mean = static_cast<float>(T_upper_sum / fn);
    s.wind_upper_mean = static_cast<float>(wind_upper_sum / fn);
    s.stability_mean = static_cast<float>(stability_sum / fn);

    // Phase 4: energy budget
    s.total_energy = atmo.total_energy;
    if (std::abs(atmo.initial_energy) > 0.01f) {
        s.energy_drift_pct =
            (atmo.total_energy - atmo.initial_energy) / std::abs(atmo.initial_energy) * 100.0f;
    }

    // Variation metrics (standard deviation via Var = E[X^2] - E[X]^2)
    auto stddev = [](double sum2, double sum, float count) -> float {
        double mean = sum / count;
        double var = sum2 / count - mean * mean;
        return static_cast<float>(std::sqrt(std::max(0.0, var)));
    };
    s.T_stddev = stddev(T_sum2, T_sum, fn);
    s.wind_stddev = stddev(wind_sum2, wind_sum, fn);
    s.q_stddev = stddev(q_sum2, q_sum, fn);
    s.precip_stddev = stddev(precip_sum2, precip_sum, fn);

    // Wind direction spread: 1 - mean resultant length of unit wind vectors
    // 0 = all winds point same direction, 1 = uniformly distributed directions
    double mr_len = std::sqrt(wind_u_sum * wind_u_sum + wind_v_sum * wind_v_sum) / n;
    s.wind_dir_spread = static_cast<float>(1.0 - mr_len);

    // Invariant monitoring
    s.total_atmo_water = atmo.total_water;
    s.water_correction = atmo.last_water_correction;
    float lower_courant = s.wind_max * ADVECT_SPEED * atmo.last_dt_days;
    float upper_courant = s.wind_upper_max * UPPER_ADVECT_SPEED * atmo.last_dt_days;
    s.max_courant = std::max(lower_courant, upper_courant);

    for (int b = 0; b < 3; ++b) {
        if (band_count[b] > 0) {
            auto bc = static_cast<float>(band_count[b]);
            s.T_band[b] = static_cast<float>(band_T[b] / bc);
            s.wind_band[b] = static_cast<float>(band_wind[b] / bc);
            s.q_band[b] = static_cast<float>(band_q[b] / bc);
        }
    }

    return s;
}

// ── Detailed diagnostics (for GPT behavior tuning) ──────────────────────

void print_atmosphere_diagnostics(const AtmosphereState& atmo) {
    if (atmo.cells.empty())
        return;

    uint32_t aw = atmo.width, ah = atmo.height;
    size_t n = atmo.cells.size();

    // Collect per-cell values for distributions
    std::vector<float> T_vals, T_base_vals, T_anom_vals, q_vals, rh_vals;
    std::vector<float> cloud_vals, precip_vals, storm_vals, wind_vals, wind_upper_vals;
    std::vector<float> stability_vals, budget_vals, p_vals, grad_p_vals;
    T_vals.reserve(n);
    T_base_vals.reserve(n);
    T_anom_vals.reserve(n);
    q_vals.reserve(n);
    rh_vals.reserve(n);
    cloud_vals.reserve(n);
    precip_vals.reserve(n);
    storm_vals.reserve(n);
    wind_vals.reserve(n);
    wind_upper_vals.reserve(n);
    stability_vals.reserve(n);
    budget_vals.reserve(n);
    p_vals.reserve(n);

    // Clamp/saturation counters
    uint32_t storm_at_1 = 0, wind_at_cap = 0, cloud_at_0 = 0;
    uint32_t q_near_0 = 0, q_near_qsat = 0, precip_at_0 = 0, precip_active = 0;
    uint32_t rh_above_1 = 0, budget_near_0 = 0;

    // Land vs ocean accumulators
    double ocean_T_sum = 0, ocean_q_sum = 0, ocean_cloud_sum = 0, ocean_precip_sum = 0;
    double ocean_wind_sum = 0;
    uint32_t ocean_count = 0;
    double land_T_sum = 0, land_q_sum = 0, land_cloud_sum = 0, land_precip_sum = 0;
    double land_wind_sum = 0;
    uint32_t land_count = 0;

    // 16-band latitude accumulators
    constexpr int NBANDS = 16;
    struct LatBand {
        double T = 0, T_base = 0, T_anom = 0, q = 0, cloud = 0, precip = 0;
        double wind = 0, u = 0, v = 0, storm = 0, stability = 0;
        double wind_p95_accum = 0, storm_p95_accum = 0;
        uint32_t count = 0, inversion = 0;
    };
    LatBand bands[NBANDS];

    for (uint32_t ay = 0; ay < ah; ++ay) {
        int band = std::clamp(static_cast<int>(ay * NBANDS / ah), 0, NBANDS - 1);
        for (uint32_t ax = 0; ax < aw; ++ax) {
            const auto& c = atmo.cells[ay * aw + ax];
            float spd = std::sqrt(c.u * c.u + c.v * c.v);
            float spd_upper = std::sqrt(c.u_upper * c.u_upper + c.v_upper * c.v_upper);

            // q_sat from Clausius-Clapeyron (same formula used in tick)
            float q_sat = std::clamp(0.05f * std::exp(0.06f * c.T), 0.02f, 1.0f);
            float rh = q_sat > 0.0f ? c.q / q_sat : 0.0f;

            T_vals.push_back(c.T);
            T_base_vals.push_back(c.T_base);
            T_anom_vals.push_back(c.T_anom);
            q_vals.push_back(c.q);
            rh_vals.push_back(rh);
            cloud_vals.push_back(c.cloud);
            precip_vals.push_back(c.precip_rate);
            storm_vals.push_back(c.storminess);
            wind_vals.push_back(spd);
            wind_upper_vals.push_back(spd_upper);
            stability_vals.push_back(c.stability);
            budget_vals.push_back(c.precip_budget);
            p_vals.push_back(c.p);

            // Clamp fractions
            if (c.storminess >= 0.999f)
                ++storm_at_1;
            if (spd >= MAX_WIND_SPEED - 0.1f)
                ++wind_at_cap;
            if (c.cloud < 0.001f)
                ++cloud_at_0;
            if (c.q < 0.005f)
                ++q_near_0;
            if (rh > 0.95f)
                ++q_near_qsat;
            if (rh > 1.0f)
                ++rh_above_1;
            if (c.precip_rate < 0.001f)
                ++precip_at_0;
            if (c.precip_rate > 0.01f)
                ++precip_active;
            if (c.precip_budget < 0.05f)
                ++budget_near_0;

            // Land vs ocean
            if (c.is_water) {
                ocean_T_sum += c.T;
                ocean_q_sum += c.q;
                ocean_cloud_sum += c.cloud;
                ocean_precip_sum += c.precip_rate;
                ocean_wind_sum += spd;
                ++ocean_count;
            } else {
                land_T_sum += c.T;
                land_q_sum += c.q;
                land_cloud_sum += c.cloud;
                land_precip_sum += c.precip_rate;
                land_wind_sum += spd;
                ++land_count;
            }

            // Latitude bands
            auto& b = bands[band];
            b.T += c.T;
            b.T_base += c.T_base;
            b.T_anom += c.T_anom;
            b.q += c.q;
            b.cloud += c.cloud;
            b.precip += c.precip_rate;
            b.wind += spd;
            b.u += c.u;
            b.v += c.v;
            b.storm += c.storminess;
            b.stability += c.stability;
            b.count++;
            if (c.stability > INVERSION_THRESHOLD)
                ++b.inversion;

            // Pressure gradient magnitude (finite difference)
            if (ax > 0 && ax < aw - 1 && ay > 0 && ay < ah - 1) {
                float dpdx =
                    (atmo.cells[ay * aw + ax + 1].p - atmo.cells[ay * aw + ax - 1].p) * 0.5f;
                float dpdy =
                    (atmo.cells[(ay + 1) * aw + ax].p - atmo.cells[(ay - 1) * aw + ax].p) * 0.5f;
                grad_p_vals.push_back(std::sqrt(dpdx * dpdx + dpdy * dpdy));
            }
        }
    }

    // Sort all vectors for percentile computation
    auto pct = [](std::vector<float>& v, float p) -> float {
        if (v.empty())
            return 0.0f;
        std::sort(v.begin(), v.end());
        size_t idx = static_cast<size_t>(p * static_cast<float>(v.size() - 1));
        return v[std::min(idx, v.size() - 1)];
    };
    // Sort once, compute multiple percentiles
    for (auto* vec : {&T_vals, &T_base_vals, &T_anom_vals, &q_vals, &rh_vals, &cloud_vals,
                      &precip_vals, &storm_vals, &wind_vals, &wind_upper_vals, &stability_vals,
                      &budget_vals, &p_vals, &grad_p_vals}) {
        std::sort(vec->begin(), vec->end());
    }

    auto print_pct = [](const char* name, std::vector<float>& v) {
        auto p = [&v](float f) -> float {
            if (v.empty())
                return 0.0f;
            size_t idx = static_cast<size_t>(f * static_cast<float>(v.size() - 1));
            return v[std::min(idx, v.size() - 1)];
        };
        printf("  %-14s p01=%8.4f p05=%8.4f p25=%8.4f p50=%8.4f p75=%8.4f p95=%8.4f p99=%8.4f\n",
               name, p(0.01f), p(0.05f), p(0.25f), p(0.50f), p(0.75f), p(0.95f), p(0.99f));
    };

    printf("\n=== DETAILED ATMOSPHERE DIAGNOSTICS ===\n");

    // 1. Field distributions
    printf("\n--- FIELD DISTRIBUTIONS (percentiles) ---\n");
    print_pct("T", T_vals);
    print_pct("T_base", T_base_vals);
    print_pct("T_anom", T_anom_vals);
    print_pct("q", q_vals);
    print_pct("RH (q/qsat)", rh_vals);
    print_pct("cloud", cloud_vals);
    print_pct("precip_rate", precip_vals);
    print_pct("storminess", storm_vals);
    print_pct("|V| lower", wind_vals);
    print_pct("|V| upper", wind_upper_vals);
    print_pct("stability", stability_vals);
    print_pct("precip_budget", budget_vals);
    print_pct("pressure", p_vals);
    print_pct("|grad_p|", grad_p_vals);

    // 2. Clamp/saturation fractions
    auto fn = static_cast<float>(n);
    printf("\n--- CLAMP/SATURATION FRACTIONS ---\n");
    printf("  storm_at_1.0:    %6u / %zu (%.1f%%)\n", storm_at_1, n, 100.0f * storm_at_1 / fn);
    printf("  wind_at_cap:     %6u / %zu (%.1f%%)\n", wind_at_cap, n, 100.0f * wind_at_cap / fn);
    printf("  cloud_at_0:      %6u / %zu (%.1f%%)\n", cloud_at_0, n, 100.0f * cloud_at_0 / fn);
    printf("  q_near_0:        %6u / %zu (%.1f%%)\n", q_near_0, n, 100.0f * q_near_0 / fn);
    printf("  q_near_qsat:     %6u / %zu (%.1f%%)\n", q_near_qsat, n, 100.0f * q_near_qsat / fn);
    printf("  RH > 1.0:        %6u / %zu (%.1f%%)\n", rh_above_1, n, 100.0f * rh_above_1 / fn);
    printf("  precip_at_0:     %6u / %zu (%.1f%%)\n", precip_at_0, n, 100.0f * precip_at_0 / fn);
    printf("  precip_active:   %6u / %zu (%.1f%%)\n", precip_active, n,
           100.0f * precip_active / fn);
    printf("  budget_near_0:   %6u / %zu (%.1f%%)\n", budget_near_0, n,
           100.0f * budget_near_0 / fn);

    // 3. Land vs ocean
    printf("\n--- LAND vs OCEAN ---\n");
    printf("  %-12s %8s %8s %8s %8s %8s\n", "", "T_mean", "q_mean", "cloud", "precip", "wind");
    if (ocean_count > 0) {
        auto oc = static_cast<float>(ocean_count);
        printf("  %-12s %8.2f %8.4f %8.4f %8.4f %8.3f  (n=%u)\n", "Ocean", ocean_T_sum / oc,
               ocean_q_sum / oc, ocean_cloud_sum / oc, ocean_precip_sum / oc, ocean_wind_sum / oc,
               ocean_count);
    }
    if (land_count > 0) {
        auto lc = static_cast<float>(land_count);
        printf("  %-12s %8.2f %8.4f %8.4f %8.4f %8.3f  (n=%u)\n", "Land", land_T_sum / lc,
               land_q_sum / lc, land_cloud_sum / lc, land_precip_sum / lc, land_wind_sum / lc,
               land_count);
    }

    // 4. 16-band latitude breakdown
    printf("\n--- 16-BAND LATITUDE BREAKDOWN ---\n");
    printf("  %4s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %5s\n", "band", "T_mean", "T_base",
           "T_anom", "q_mean", "cloud", "precip", "wind", "u_mean", "v_mean", "storm", "stab",
           "inv%%");
    for (int i = 0; i < NBANDS; ++i) {
        auto& b = bands[i];
        if (b.count == 0)
            continue;
        auto bc = static_cast<float>(b.count);
        printf("  %4d %7.2f %7.2f %7.2f %7.4f %7.4f %7.4f %7.3f %7.3f %7.3f %7.4f %7.2f %5.1f\n", i,
               b.T / bc, b.T_base / bc, b.T_anom / bc, b.q / bc, b.cloud / bc, b.precip / bc,
               b.wind / bc, b.u / bc, b.v / bc, b.storm / bc, b.stability / bc,
               100.0f * b.inversion / bc);
    }

    // 5. Energy budget (decomposed)
    printf("\n--- ENERGY BUDGET ---\n");
    double E_lower = 0, E_upper = 0, E_latent = 0, E_ground = 0;
    double T_upper_sum_diag = 0;
    for (const auto& c : atmo.cells) {
        E_lower += c.T;
        E_upper += c.T_upper;
        E_latent += LATENT_ENERGY * c.cloud;
        E_ground += c.ground_temp;
        T_upper_sum_diag += c.T_upper;
    }
    float fn_diag = static_cast<float>(atmo.cells.size());
    printf("  E_lower:  %.1f  (T_mean=%.2f)\n", E_lower, E_lower / fn_diag);
    printf("  E_upper:  %.1f  (T_upper_mean=%.2f)\n", E_upper, T_upper_sum_diag / fn_diag);
    printf("  E_latent: %.1f  (cloud_mean=%.4f)\n", E_latent, E_latent / LATENT_ENERGY / fn_diag);
    printf("  E_ground: %.1f  (ground_mean=%.2f)\n", E_ground, E_ground / fn_diag);
    printf("  total_energy:  %.1f\n", atmo.total_energy);
    printf("  initial_energy: %.1f\n", atmo.initial_energy);
    if (std::abs(atmo.initial_energy) > 0.01f) {
        printf("  energy_drift: %.3f%%\n",
               (atmo.total_energy - atmo.initial_energy) / std::abs(atmo.initial_energy) * 100.0f);
    }

    // 6. Water budget snapshot
    printf("\n--- WATER STATE ---\n");
    double q_total = 0, cloud_total = 0, q_upper_total = 0;
    for (const auto& c : atmo.cells) {
        q_total += c.q;
        cloud_total += c.cloud;
        q_upper_total += c.q_upper;
    }
    printf("  sum(q_lower):   %.4f\n", q_total);
    printf("  sum(cloud):     %.4f\n", cloud_total);
    printf("  sum(q_upper):   %.4f\n", q_upper_total);
    printf("  sum(q+cloud):   %.4f  (tracked total_water: %.4f)\n", q_total + cloud_total,
           atmo.total_water);
    printf("  water_correction_last: %.6f\n", atmo.last_water_correction);

    // 6b. QG field spatial structure
    printf("\n--- QG FIELDS (sample rows) ---\n");
    // Print psi1, psi2 at 5 evenly spaced rows to see spatial structure
    for (int ri = 0; ri < 5; ++ri) {
        uint32_t row = static_cast<uint32_t>(ri) * (ah - 1) / 4;
        if (row >= ah)
            row = ah - 1;
        printf("  psi1 row %2u: ", row);
        for (uint32_t ax = 0; ax < aw; ax += aw / 16) {
            printf("%7.2f", atmo.cells[row * aw + ax].psi1);
        }
        printf("\n  psi2 row %2u: ", row);
        for (uint32_t ax = 0; ax < aw; ax += aw / 16) {
            printf("%7.2f", atmo.cells[row * aw + ax].psi2);
        }
        printf("\n  q1pv row %2u: ", row);
        for (uint32_t ax = 0; ax < aw; ax += aw / 16) {
            printf("%7.2f", atmo.cells[row * aw + ax].q1_pv);
        }
        printf("\n  u    row %2u: ", row);
        for (uint32_t ax = 0; ax < aw; ax += aw / 16) {
            printf("%7.2f", atmo.cells[row * aw + ax].u);
        }
        printf("\n  v    row %2u: ", row);
        for (uint32_t ax = 0; ax < aw; ax += aw / 16) {
            printf("%7.2f", atmo.cells[row * aw + ax].v);
        }
        printf("\n");
    }

    // Psi1 statistics
    float psi1_min = 1e9f, psi1_max = -1e9f, psi2_min = 1e9f, psi2_max = -1e9f;
    double psi1_sum2 = 0, psi2_sum2 = 0;
    for (const auto& c : atmo.cells) {
        psi1_min = std::min(psi1_min, c.psi1);
        psi1_max = std::max(psi1_max, c.psi1);
        psi2_min = std::min(psi2_min, c.psi2);
        psi2_max = std::max(psi2_max, c.psi2);
        psi1_sum2 += static_cast<double>(c.psi1) * c.psi1;
        psi2_sum2 += static_cast<double>(c.psi2) * c.psi2;
    }
    printf("  psi1 range: [%.3f, %.3f]  rms=%.3f\n", psi1_min, psi1_max,
           std::sqrt(psi1_sum2 / static_cast<double>(n)));
    printf("  psi2 range: [%.3f, %.3f]  rms=%.3f\n", psi2_min, psi2_max,
           std::sqrt(psi2_sum2 / static_cast<double>(n)));

    // 7. Boundary row diagnostics (north edge = row 0, south edge = row ah-1)
    printf("\n--- BOUNDARY ROWS ---\n");
    auto row_stats = [&](uint32_t row, const char* label) {
        double rT = 0, rv = 0, ru = 0, rq = 0;
        for (uint32_t ax = 0; ax < aw; ++ax) {
            const auto& c = atmo.cells[row * aw + ax];
            rT += c.T;
            rv += c.v;
            ru += c.u;
            rq += c.q;
        }
        float faw = static_cast<float>(aw);
        printf("  %-12s T_mean=%7.2f  v_mean=%7.4f  u_mean=%7.4f  q_mean=%7.4f\n", label, rT / faw,
               rv / faw, ru / faw, rq / faw);
    };
    row_stats(0, "North edge");
    row_stats(1, "North+1");
    row_stats(ah - 2, "South-1");
    row_stats(ah - 1, "South edge");

    // 8. Courant/CFL
    printf("\n--- CFL DIAGNOSTICS ---\n");
    float max_courant_lower = 0, max_courant_upper = 0;
    for (const auto& c : atmo.cells) {
        float spd = std::sqrt(c.u * c.u + c.v * c.v);
        float spd_u = std::sqrt(c.u_upper * c.u_upper + c.v_upper * c.v_upper);
        max_courant_lower = std::max(max_courant_lower, spd * ADVECT_SPEED * atmo.last_dt_days);
        max_courant_upper =
            std::max(max_courant_upper, spd_u * UPPER_ADVECT_SPEED * atmo.last_dt_days);
    }
    printf("  max_courant_lower: %.4f\n", max_courant_lower);
    printf("  max_courant_upper: %.4f\n", max_courant_upper);
    printf("  MAX_TRACE_DIST: %.2f\n", MAX_TRACE_DIST);
    printf("  MAX_WIND_SPEED: %.2f\n", MAX_WIND_SPEED);

    // 9. RH supersaturation details
    printf("\n--- RELATIVE HUMIDITY DETAILS ---\n");
    double rh_sum = 0, supersaturation_sum = 0;
    uint32_t supersat_count = 0;
    for (size_t i = 0; i < atmo.cells.size(); ++i) {
        rh_sum += rh_vals[i];  // already sorted but sum doesn't care
    }
    // Use unsorted data for mean supersaturation
    for (const auto& c : atmo.cells) {
        float q_sat = std::clamp(0.05f * std::exp(0.06f * c.T), 0.02f, 1.0f);
        float excess = c.q - q_sat;
        if (excess > 0.0f) {
            supersaturation_sum += excess;
            ++supersat_count;
        }
    }
    printf("  RH_mean: %.4f\n", rh_sum / static_cast<float>(n));
    printf("  supersaturated cells: %u (%.1f%%)\n", supersat_count, 100.0f * supersat_count / fn);
    if (supersat_count > 0) {
        printf("  mean supersaturation (q-qsat): %.6f\n", supersaturation_sum / supersat_count);
    }
}

// ── Variability diagnostics ──────────────────────────────────────────

void print_variability_diagnostics(const AtmosphereState& atmo) {
    if (atmo.cells.empty())
        return;
    uint32_t aw = atmo.width, ah = atmo.height;

    // Collect vorticity, divergence, gradient magnitudes
    std::vector<float> vort_vals, div_vals, grad_T_vals, grad_p_vals, grad_q_vals;
    std::vector<float> land_rh_vals, land_q_vals;
    uint32_t conv_trigger_land = 0, land_cells = 0;

    for (uint32_t ay = 1; ay + 1 < ah; ++ay) {
        for (uint32_t ax = 1; ax + 1 < aw; ++ax) {
            const auto& c = atmo.cell_at(ax, ay);
            const auto& cl = atmo.cell_at(ax - 1, ay);
            const auto& cr = atmo.cell_at(ax + 1, ay);
            const auto& cu = atmo.cell_at(ax, ay - 1);
            const auto& cd = atmo.cell_at(ax, ay + 1);

            // Vorticity: dv/dx - du/dy
            float dvdx = (cr.v - cl.v) * 0.5f;
            float dudy = (cd.u - cu.u) * 0.5f;
            vort_vals.push_back(dvdx - dudy);

            // Divergence: du/dx + dv/dy
            float dudx = (cr.u - cl.u) * 0.5f;
            float dvdy = (cd.v - cu.v) * 0.5f;
            div_vals.push_back(dudx + dvdy);

            // Temperature gradient
            float dTdx = (cr.T - cl.T) * 0.5f;
            float dTdy = (cd.T - cu.T) * 0.5f;
            grad_T_vals.push_back(std::sqrt(dTdx * dTdx + dTdy * dTdy));

            // Pressure gradient
            float dpdx = (cr.p - cl.p) * 0.5f;
            float dpdy = (cd.p - cu.p) * 0.5f;
            grad_p_vals.push_back(std::sqrt(dpdx * dpdx + dpdy * dpdy));

            // Humidity gradient
            float dqdx = (cr.q - cl.q) * 0.5f;
            float dqdy = (cd.q - cu.q) * 0.5f;
            grad_q_vals.push_back(std::sqrt(dqdx * dqdx + dqdy * dqdy));

            // Land-specific RH and convective trigger
            if (!c.is_water) {
                float q_sat = std::clamp(0.05f * std::exp(0.06f * c.T), 0.02f, 1.0f);
                float rh = c.q / q_sat;
                land_rh_vals.push_back(rh);
                land_q_vals.push_back(c.q);
                land_cells++;
                if (c.ground_temp - c.T > CONVECTIVE_THRESHOLD && c.q > q_sat * 0.5f)
                    conv_trigger_land++;
            }
        }
    }

    // Sort for percentiles
    auto pctf = [](std::vector<float>& v, float p) -> float {
        if (v.empty())
            return 0.0f;
        size_t idx = static_cast<size_t>(p * static_cast<float>(v.size() - 1));
        return v[std::min(idx, v.size() - 1)];
    };
    for (auto* vec : {&vort_vals, &div_vals, &grad_T_vals, &grad_p_vals, &grad_q_vals,
                      &land_rh_vals, &land_q_vals}) {
        std::sort(vec->begin(), vec->end());
    }

    // Also compute absolute vorticity and divergence percentiles
    std::vector<float> abs_vort, abs_div;
    for (float v : vort_vals)
        abs_vort.push_back(std::abs(v));
    for (float d : div_vals)
        abs_div.push_back(std::abs(d));
    std::sort(abs_vort.begin(), abs_vort.end());
    std::sort(abs_div.begin(), abs_div.end());

    printf("\n=== VARIABILITY DIAGNOSTICS ===\n");

    printf("\n--- VORTICITY (dv/dx - du/dy) ---\n");
    printf("  signed:  p01=%.4f p05=%.4f p50=%.4f p95=%.4f p99=%.4f\n", pctf(vort_vals, 0.01f),
           pctf(vort_vals, 0.05f), pctf(vort_vals, 0.50f), pctf(vort_vals, 0.95f),
           pctf(vort_vals, 0.99f));
    printf("  |vort|:  p50=%.4f p90=%.4f p99=%.4f max=%.4f\n", pctf(abs_vort, 0.50f),
           pctf(abs_vort, 0.90f), pctf(abs_vort, 0.99f), abs_vort.empty() ? 0.0f : abs_vort.back());

    printf("\n--- DIVERGENCE (du/dx + dv/dy) ---\n");
    printf("  signed:  p01=%.4f p05=%.4f p50=%.4f p95=%.4f p99=%.4f\n", pctf(div_vals, 0.01f),
           pctf(div_vals, 0.05f), pctf(div_vals, 0.50f), pctf(div_vals, 0.95f),
           pctf(div_vals, 0.99f));
    printf("  |div|:   p50=%.4f p90=%.4f p99=%.4f max=%.4f\n", pctf(abs_div, 0.50f),
           pctf(abs_div, 0.90f), pctf(abs_div, 0.99f), abs_div.empty() ? 0.0f : abs_div.back());

    printf("\n--- GRADIENT MAGNITUDES ---\n");
    printf("  |grad_T|: p50=%.4f p90=%.4f p99=%.4f max=%.4f\n", pctf(grad_T_vals, 0.50f),
           pctf(grad_T_vals, 0.90f), pctf(grad_T_vals, 0.99f),
           grad_T_vals.empty() ? 0.0f : grad_T_vals.back());
    printf("  |grad_p|: p50=%.4f p90=%.4f p99=%.4f max=%.4f\n", pctf(grad_p_vals, 0.50f),
           pctf(grad_p_vals, 0.90f), pctf(grad_p_vals, 0.99f),
           grad_p_vals.empty() ? 0.0f : grad_p_vals.back());
    printf("  |grad_q|: p50=%.4f p90=%.4f p99=%.4f max=%.4f\n", pctf(grad_q_vals, 0.50f),
           pctf(grad_q_vals, 0.90f), pctf(grad_q_vals, 0.99f),
           grad_q_vals.empty() ? 0.0f : grad_q_vals.back());

    printf("\n--- LAND MOISTURE / CONVECTION ---\n");
    printf("  land_RH:  p10=%.4f p25=%.4f p50=%.4f p75=%.4f p90=%.4f p99=%.4f\n",
           pctf(land_rh_vals, 0.10f), pctf(land_rh_vals, 0.25f), pctf(land_rh_vals, 0.50f),
           pctf(land_rh_vals, 0.75f), pctf(land_rh_vals, 0.90f), pctf(land_rh_vals, 0.99f));
    printf("  land_q:   p10=%.4f p25=%.4f p50=%.4f p75=%.4f p90=%.4f\n", pctf(land_q_vals, 0.10f),
           pctf(land_q_vals, 0.25f), pctf(land_q_vals, 0.50f), pctf(land_q_vals, 0.75f),
           pctf(land_q_vals, 0.90f));
    printf("  convective_trigger: %u of %u land cells (%.2f%%)\n", conv_trigger_land, land_cells,
           land_cells > 0 ? 100.0f * conv_trigger_land / land_cells : 0.0f);

    // Variance by wavenumber (crude 1D power spectrum along x for each row, averaged)
    // Gives scale of dominant features
    printf("\n--- SPATIAL SCALE (T_anom variance by x-wavenumber, row-averaged) ---\n");
    std::vector<double> power_by_k(aw / 2 + 1, 0.0);
    for (uint32_t ay = 0; ay < ah; ++ay) {
        // Compute row mean
        double row_mean = 0;
        for (uint32_t ax = 0; ax < aw; ++ax)
            row_mean += atmo.cell_at(ax, ay).T_anom;
        row_mean /= aw;
        // DFT for this row
        for (uint32_t k = 0; k <= aw / 2; ++k) {
            double re = 0, im = 0;
            for (uint32_t ax = 0; ax < aw; ++ax) {
                double angle = 2.0 * 3.14159265358979 * k * ax / aw;
                double val = atmo.cell_at(ax, ay).T_anom - row_mean;
                re += val * std::cos(angle);
                im += val * std::sin(angle);
            }
            power_by_k[k] += (re * re + im * im) / (aw * aw);
        }
    }
    // Average over rows
    for (auto& p : power_by_k)
        p /= ah;
    // Print grouped
    printf("  k=1-2 (planetary): %.4f\n", power_by_k[1] + power_by_k[2]);
    double mid_sum = 0;
    for (uint32_t k = 3; k <= 8; ++k)
        mid_sum += power_by_k[k];
    printf("  k=3-8 (synoptic):  %.4f\n", mid_sum);
    double meso_sum = 0;
    for (uint32_t k = 9; k <= aw / 2; ++k)
        meso_sum += power_by_k[k];
    printf("  k=9-%u (meso):     %.4f\n", aw / 2, meso_sum);
    double total_power = 0;
    for (uint32_t k = 1; k <= aw / 2; ++k)
        total_power += power_by_k[k];
    if (total_power > 0) {
        printf("  synoptic fraction: %.1f%%\n", 100.0 * mid_sum / total_power);
        printf("  meso fraction:     %.1f%%\n", 100.0 * meso_sum / total_power);
    }

    // Count pressure extrema (local min/max of p_anom)
    uint32_t p_min_count = 0, p_max_count = 0;
    for (uint32_t ay = 1; ay + 1 < ah; ++ay) {
        for (uint32_t ax = 1; ax + 1 < aw; ++ax) {
            float p = atmo.cell_at(ax, ay).p_anom;
            bool is_min = true, is_max = true;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    float pn = atmo.cell_at(ax + dx, ay + dy).p_anom;
                    if (pn <= p)
                        is_min = false;
                    if (pn >= p)
                        is_max = false;
                }
            }
            if (is_min && p < -1.0f)
                p_min_count++;
            if (is_max && p > 1.0f)
                p_max_count++;
        }
    }
    printf("\n--- PRESSURE EXTREMA (p_anom) ---\n");
    printf("  lows (< -1 hPa): %u  highs (> +1 hPa): %u\n", p_min_count, p_max_count);
}

}  // namespace sandbox

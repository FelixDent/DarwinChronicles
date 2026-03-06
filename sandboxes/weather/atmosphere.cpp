#include "atmosphere.h"

#include <algorithm>
#include <cmath>

#include "dynamics.h"

namespace sandbox {

// ── Constants ──────────────────────────────────────────────────────────

static constexpr float PI = 3.14159265358979f;

// Temperature
static constexpr float K_SOLAR = 8.0f;         // solar heating strength (compensates for lack of vertical Hadley cell)
static constexpr float K_RAD = 0.15f;          // radiative cooling rate (balanced with stronger solar)
static constexpr float K_GROUND_AIR = 0.25f;   // ground-air coupling (stronger — air follows ground gradient)
static constexpr float K_AIR_GROUND = 0.15f;   // air-ground coupling (reverse)
static constexpr float K_EVAP_COOL = 1.5f;     // evaporative cooling
static constexpr float LAPSE_PER_ELEV = 35.0f;  // temp drop per unit elevation
static constexpr float K_LATENT = 8.0f;        // latent heat per unit condensation (C per unit q)
static constexpr float K_RELAX_T = 0.05f;       // air temp relaxation toward T_ref (gentle — allows weather fronts)

// Pressure / wind
static constexpr float K_PRESSURE = 0.18f;    // wind acceleration from pressure gradient
static constexpr float K_DAMP = 0.05f;        // wind damping per day (gentle — wind persists)
static constexpr float K_DRAG = 1.0f;         // terrain roughness drag coefficient
static constexpr float P_TEMP_COEFF = 0.8f;   // pressure response to temperature (lower = less meridional mixing)
static constexpr float K_TURBULENCE = 0.25f;  // thermal turbulence strength
static constexpr float K_CONVERGENCE = 1.5f;  // convergence/divergence pressure feedback

// Advection
static constexpr float ADVECT_SPEED = 0.5f;  // advection scaling (lower = less numerical gradient erosion)

// Moisture
static constexpr float OCEAN_EVAP_RATE = 0.15f;    // ocean moisture source per day (low — prevents uniform cloud)
static constexpr float LAND_EVAP_RATE = 0.05f;     // land moisture source per day (from soil)
static constexpr float CLOUD_PRECIP_RATE = 1.2f;       // cloud → precipitation per day (fast drain above threshold)
static constexpr float CLOUD_PRECIP_THRESHOLD = 0.12f; // minimum cloud for precipitation (needs thick cloud)
static constexpr float CLOUD_EVAP_BASE = 0.15f;    // base cloud dissipation per day (thin cloud erodes)
static constexpr float CLOUD_EVAP_DRY = 0.40f;     // additional dissipation in dry air
static constexpr float CLOUD_SUBSIDE_RATE = 0.50f; // cloud clearing from high-pressure subsidence
static constexpr float UPLIFT_STRENGTH = 4.0f;     // orographic uplift multiplier

// Wind-temperature coupling
static constexpr float K_CONVECTIVE = 0.08f;  // wind convective cooling/heating on ground

// Diffusion (invariant 5 & 6: heat and moisture spread via gradients)
static constexpr float K_HEAT_DIFFUSE = 0.04f;    // heat diffusion rate per day (gentle — preserves fronts)
static constexpr float K_MOIST_DIFFUSE = 0.03f;   // moisture diffusion rate per day

// Numerical stability (invariant 10: Courant condition)
static constexpr float MAX_WIND_SPEED = 6.0f;     // hard cap on wind speed (cells/day)
static constexpr float MAX_TRACE_DIST = 1.5f;     // max advection trace per step (cells)

// Convective instability (invariant 7: storms release energy)
static constexpr float CONVECTIVE_THRESHOLD = 6.0f;  // ground-air T diff triggering convection (C)
static constexpr float CONVECTIVE_PRECIP = 0.15f;    // enhanced precip rate during convection

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
                    if (tt.is_ocean || tt.is_lake)
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
                cell.is_water = water_count > count / 2;

                // Initialize q at saturation for the local temperature.
                // Baked moisture is a normalized index [0,1], NOT specific humidity.
                // Cap at q_sat so we don't get a catastrophic initial condensation burst.
                float q_sat = std::clamp(0.05f * std::exp(0.06f * cell.T), 0.02f, 1.0f);
                float baked_moisture = sum_q / n;
                cell.q = std::clamp(baked_moisture * q_sat, 0.0f, q_sat);
            }

            cell.ground_temp = cell.T;
            cell.p = 1013.0f;
            cell.cloud = 0.0f;
            cell.precip_rate = 0.0f;
            cell.avg_snow = 0.0f;
            cell.avg_soil_wet = cell.is_water ? 1.0f : cell.q;
        }
    }
}

// ── Bilinear helper for advection ──────────────────────────────────────

static float sample_field(const std::vector<AtmosphereCell>& buf, uint32_t w, uint32_t h, float fx,
                          float fy, float AtmosphereCell::*field) {
    fx = std::clamp(fx, 0.0f, static_cast<float>(w - 1));
    fy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));

    auto x0 = static_cast<int>(fx);
    auto y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, static_cast<int>(w - 1));
    int y1 = std::min(y0 + 1, static_cast<int>(h - 1));
    float sx = fx - static_cast<float>(x0);
    float sy = fy - static_cast<float>(y0);

    float v00 = buf[static_cast<size_t>(y0) * w + static_cast<size_t>(x0)].*field;
    float v10 = buf[static_cast<size_t>(y0) * w + static_cast<size_t>(x1)].*field;
    float v01 = buf[static_cast<size_t>(y1) * w + static_cast<size_t>(x0)].*field;
    float v11 = buf[static_cast<size_t>(y1) * w + static_cast<size_t>(x1)].*field;

    return (v00 * (1.0f - sx) + v10 * sx) * (1.0f - sy) +
           (v01 * (1.0f - sx) + v11 * sx) * sy;
}

// ── Tick ───────────────────────────────────────────────────────────────

void tick_atmosphere(AtmosphereState& atmo, const Terrain& world, const DynamicState& dyn,
                     float dt_seconds) {
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

    // ── Step 2: Pressure from temperature ────────────────────────────────
    // Uses previous tick's temperature field to drive pressure gradients.
    // Must happen BEFORE advection so wind field is ready for transport.

    // Compute global mean temperature for pressure reference
    float T_mean = 0.0f;
    for (const auto& cell : atmo.cells)
        T_mean += cell.T;
    T_mean /= static_cast<float>(atmo.cells.size());

    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);

            // Thermal pressure: warm → low, cool → high
            cell.p = 1013.0f - P_TEMP_COEFF * (cell.T - T_mean);
            // Elevation reduces pressure
            cell.p -= cell.avg_elevation * 40.0f;

            // Convergence/divergence feedback: wind converging into an area
            // creates low pressure (rising air), divergence creates high pressure.
            // This lets weather systems (lows/highs) self-organize and intensify.
            float div = 0.0f;
            if (ax > 0 && ax < aw - 1) {
                div += (atmo.cell_at(ax + 1, ay).u - atmo.cell_at(ax - 1, ay).u) * 0.5f;
            }
            if (ay > 0 && ay < ah - 1) {
                div += (atmo.cell_at(ax, ay + 1).v - atmo.cell_at(ax, ay - 1).v) * 0.5f;
            }
            // Negative divergence (convergence) → lower pressure
            cell.p += K_CONVERGENCE * div;

            // Moisture loading: humid air is lighter → lower pressure
            cell.p -= cell.q * 2.0f;
        }
    }

    // ── Step 3: Wind from pressure gradients ───────────────────────────

    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);

            // Pressure gradient (central difference)
            float px_left = (ax > 0) ? atmo.cell_at(ax - 1, ay).p : cell.p;
            float px_right = (ax < aw - 1) ? atmo.cell_at(ax + 1, ay).p : cell.p;
            float py_up = (ay > 0) ? atmo.cell_at(ax, ay - 1).p : cell.p;
            float py_down = (ay < ah - 1) ? atmo.cell_at(ax, ay + 1).p : cell.p;

            float dpdx = (px_right - px_left) * 0.5f;
            float dpdy = (py_down - py_up) * 0.5f;

            // Accelerate: high → low pressure
            cell.u += -K_PRESSURE * dpdx * dt_days;
            cell.v += -K_PRESSURE * dpdy * dt_days;

            // Coriolis deflection (latitude-dependent)
            // Minimum magnitude near equator prevents pure meridional flow,
            // deflecting it into zonal trade winds (matching real Hadley cell dynamics)
            float lat_frac = static_cast<float>(ay) / static_cast<float>(ah);
            float raw_coriolis = (0.5f - lat_frac) * 0.45f;
            // Minimum Coriolis prevents pure meridional flow at equator;
            // copysign ensures correct hemisphere deflection even at lat_frac==0.5
            float coriolis = std::copysign(std::max(std::abs(raw_coriolis), 0.04f),
                                           raw_coriolis) * dt_days;
            float new_u = cell.u + coriolis * cell.v;
            float new_v = cell.v - coriolis * cell.u;
            cell.u = new_u;
            cell.v = new_v;

            // Damping (air friction)
            float damp = std::max(0.0f, 1.0f - K_DAMP * dt_days);
            cell.u *= damp;
            cell.v *= damp;

            // Terrain drag (roughness + elevation features)
            float drag = 1.0f / (1.0f + K_DRAG * cell.avg_roughness * dt_days);
            cell.u *= drag;
            cell.v *= drag;

            // Thermal turbulence: multi-scale gusts that create real wind variability.
            // Fast component (~hourly): local convective gusts, stronger during afternoon
            // Slow component (~6 hours): mesoscale wind shifts from passing pressure cells
            float convective_strength = (cell.is_water ? 0.4f : 1.0f);
            // Daytime boost: afternoon thermals are stronger
            float daytime_boost = 0.5f + 0.5f * std::max(0.0f, diurnal);

            // Fast turbulence (~1 hour resolution): local gusts
            uint32_t turb_fast = static_cast<uint32_t>(atmo.time_of_day * 1.0f +
                                                        atmo.day_of_year * 24.0f);
            float tu_fast = turb_noise(ax, ay, turb_fast);
            float tv_fast = turb_noise(ax + 97u, ay + 53u, turb_fast);

            // Slow turbulence (~6 hour resolution): mesoscale shifts
            uint32_t turb_slow = static_cast<uint32_t>(atmo.time_of_day * 0.17f +
                                                        atmo.day_of_year * 4.0f);
            float tu_slow = turb_noise(ax * 3u, ay * 3u, turb_slow);
            float tv_slow = turb_noise(ax * 3u + 41u, ay * 3u + 17u, turb_slow);

            float tu = tu_fast * 0.6f + tu_slow * 0.4f;
            float tv = tv_fast * 0.6f + tv_slow * 0.4f;
            cell.u += K_TURBULENCE * convective_strength * daytime_boost * tu * dt_days;
            cell.v += K_TURBULENCE * convective_strength * daytime_boost * tv * dt_days;

            // Invariant 4+10: hard cap wind speed to prevent numerical blowup
            float spd = std::sqrt(cell.u * cell.u + cell.v * cell.v);
            if (spd > MAX_WIND_SPEED) {
                float s = MAX_WIND_SPEED / spd;
                cell.u *= s;
                cell.v *= s;
            }
        }
    }

    // ── Step 4: Semi-Lagrangian advection of T, q, cloud ───────────────
    // Invariant 2: Record atmospheric water before advection (advection should
    // conserve mass but bilinear interpolation introduces drift)
    float water_before_advect = 0.0f;
    for (const auto& c : atmo.cells)
        water_before_advect += c.q + c.cloud;
    // Copy current state, then trace back through wind field
    // Wrap x-coordinate (periodic east-west), clamp y (poles are walls)

    atmo.scratch = atmo.cells;

    auto wrap_x = [aw](float fx) -> float {
        float fw = static_cast<float>(aw);
        fx = std::fmod(fx, fw);
        if (fx < 0.0f)
            fx += fw;
        return fx;
    };

    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            const auto& src = atmo.scratch[static_cast<size_t>(ay) * aw + ax];

            // Trace backwards through wind field
            float trace_x = src.u * dt_days * ADVECT_SPEED;
            float trace_y = src.v * dt_days * ADVECT_SPEED;

            // Invariant 10: CFL limiter — clamp trace distance to prevent
            // advection overshooting grid cells (Courant condition)
            float trace_dist = std::sqrt(trace_x * trace_x + trace_y * trace_y);
            if (trace_dist > MAX_TRACE_DIST) {
                float s = MAX_TRACE_DIST / trace_dist;
                trace_x *= s;
                trace_y *= s;
            }

            float x0 = wrap_x(static_cast<float>(ax) - trace_x);
            float y0 = static_cast<float>(ay) - trace_y;

            auto& cell = atmo.cell_at(ax, ay);
            // Partial temperature advection: bilinear interpolation in semi-Lagrangian
            // causes numerical diffusion that erodes the latitude temperature gradient.
            // Retaining 15% local value compensates for this artificial mixing.
            float adv_T = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::T);
            cell.T = cell.T * 0.15f + adv_T * 0.85f;
            cell.q = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::q);
            cell.cloud = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::cloud);
            // Partially advect wind momentum — wind patterns move with the flow
            // but local pressure forcing keeps wind responsive to current conditions.
            // Full advection creates self-reinforcing jets that pipe polar cold to equator.
            float adv_u = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::u);
            float adv_v = sample_field(atmo.scratch, aw, ah, x0, y0, &AtmosphereCell::v);
            cell.u = cell.u * 0.4f + adv_u * 0.6f;
            cell.v = cell.v * 0.4f + adv_v * 0.6f;
        }
    }

    // ── Invariant 2: Correct advection-induced water drift ────────────
    // Only advection should be mass-conserving; evaporation/precipitation
    // intentionally change total water. Correct drift from bilinear interpolation.
    {
        float water_after_advect = 0.0f;
        for (const auto& c : atmo.cells)
            water_after_advect += c.q + c.cloud;

        float advect_drift = water_after_advect - water_before_advect;
        float drift_frac = (water_before_advect > 0.01f)
                               ? advect_drift / water_before_advect
                               : 0.0f;
        atmo.last_water_correction = 0.0f;
        if (std::abs(drift_frac) > 0.001f && water_after_advect > 0.01f) {
            float correction = -advect_drift / water_after_advect;
            for (auto& c : atmo.cells) {
                c.q += c.q * correction;
                c.q = std::clamp(c.q, 0.0f, 1.0f);
                c.cloud += c.cloud * correction;
                c.cloud = std::clamp(c.cloud, 0.0f, 1.0f);
            }
            atmo.last_water_correction = correction;
        }
    }

    // ── Step 4b: Heat + moisture diffusion (invariants 5 & 6) ──────────
    // Heat flows from hot→cold, moisture from wet→dry. Prevents extreme
    // gradients and smooths out numerical spikes from advection.
    atmo.scratch = atmo.cells;
    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            // East-west periodic wrap, north-south clamped (poles are walls)
            uint32_t ax_l = (ax == 0) ? aw - 1 : ax - 1;
            uint32_t ax_r = (ax == aw - 1) ? 0 : ax + 1;
            uint32_t ay_u = (ay == 0) ? 0 : ay - 1;
            uint32_t ay_d = (ay == ah - 1) ? ah - 1 : ay + 1;

            const auto& c = atmo.scratch[ay * aw + ax];
            const auto& left = atmo.scratch[ay * aw + ax_l];
            const auto& right = atmo.scratch[ay * aw + ax_r];
            const auto& up = atmo.scratch[ay_u * aw + ax];
            const auto& down = atmo.scratch[ay_d * aw + ax];

            float lap_T = left.T + right.T + up.T + down.T - 4.0f * c.T;
            float lap_q = left.q + right.q + up.q + down.q - 4.0f * c.q;

            auto& cell = atmo.cell_at(ax, ay);
            cell.T += K_HEAT_DIFFUSE * lap_T * dt_days;
            cell.q += K_MOIST_DIFFUSE * lap_q * dt_days;
            cell.q = std::max(cell.q, 0.0f);
        }
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
        float solar_seasonal = solar_base + latitude * season_offset * 0.25f;
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
            T_ref += season_offset * latitude * 12.0f;
            T_ref -= cell.avg_elevation * LAPSE_PER_ELEV;

            // Evaporative cooling from soil moisture
            float evap_cool = cell.avg_soil_wet * K_EVAP_COOL;

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

            cell.ground_temp +=
                dt_days * (K_SOLAR * solar * cloud_shade * (1.0f - albedo)
                           - radiative_cooling
                           - 0.03f * evap_cool
                           + (K_AIR_GROUND + convective) * (cell.T - cell.ground_temp));

            // Water bodies buffer temperature (high thermal inertia)
            if (cell.is_water) {
                cell.ground_temp = cell.ground_temp * 0.95f + T_ref * 0.05f;
            }

            // Air temperature: coupled to ground + nonlinear radiative relaxation
            // Small fronts (±5°C): relax ≈ 0.05*(1+1.25) = 0.11, τ ≈ 9 days (weather preserved)
            // Large drift (±20°C): relax ≈ 0.05*(1+20) = 1.05, τ < 1 day (climate restored)
            // Floor matches Stefan-Boltzmann 200K clamp to prevent ground/air target divergence
            float T_ref_air = std::max(T_ref, -73.15f);
            float delta_T = cell.T - T_ref_air;
            float raw_relax = K_RELAX_T * (1.0f + 0.05f * delta_T * delta_T);
            // Clamp so relaxation cannot overshoot T_ref in one step
            float relax_strength = std::min(raw_relax, 1.0f / (dt_days + 0.001f));
            cell.T += dt_days * (K_GROUND_AIR * (cell.ground_temp - cell.T)
                                 - relax_strength * delta_T);
        }
    }

    // ── Step 6: Moisture sources + condensation + precipitation ──────

    for (uint32_t ay = 0; ay < ah; ++ay) {
        for (uint32_t ax = 0; ax < aw; ++ax) {
            auto& cell = atmo.cell_at(ax, ay);

            // Wind speed for evaporation acceleration
            float wind_spd = std::sqrt(cell.u * cell.u + cell.v * cell.v);
            float wind_evap_factor = 1.0f + wind_spd * 0.6f;  // wind amplifies evaporation

            // Moisture sources: ocean evaporation and land evapotranspiration
            // Evaporation = f(temperature, wind, humidity deficit)
            // Saturation humidity: exponential-ish curve (Clausius-Clapeyron approx)
            // Cold air holds very little; warm air holds a lot
            float q_sat = std::clamp(0.05f * std::exp(0.06f * cell.T), 0.02f, 1.0f);

            // Humidity deficit: drier air = more evaporation
            float humidity_deficit = std::max(0.0f, 1.0f - cell.q / q_sat);

            if (cell.is_water) {
                // Ocean: strong moisture source
                float temp_factor = std::clamp(cell.T / 25.0f, 0.2f, 1.5f);
                cell.q += OCEAN_EVAP_RATE * temp_factor * wind_evap_factor * humidity_deficit *
                          dt_days;
            } else {
                // Land: moisture from soil + surface water
                // Invariant 8: evaporation cannot exceed available surface water
                float soil_source = std::min(cell.avg_soil_wet, 1.0f) * LAND_EVAP_RATE;
                float temp_factor = std::clamp(cell.T / 25.0f, 0.1f, 1.2f);
                float evap_amount = soil_source * temp_factor * wind_evap_factor * humidity_deficit *
                                    dt_days;
                // Cap evaporation by available moisture (can't evaporate more than exists)
                evap_amount = std::min(evap_amount, cell.avg_soil_wet * 0.5f);
                cell.q += evap_amount;
            }

            // Orographic uplift: wind hitting rising terrain forces condensation
            float uplift = 0.0f;
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
            uplift = std::clamp(uplift * UPLIFT_STRENGTH, 0.0f, 0.4f);

            // Uplift reduces effective saturation (forced condensation at altitude)
            float effective_qsat = q_sat - uplift * 0.3f;
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
                cell.q -= condensed;
                // Latent heat: condensation warms the air
                cell.T += condensed * K_LATENT;
            }

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
            cell.cloud -= precip;
            cell.precip_rate = std::clamp(precip / (dt_days + 0.001f), 0.0f, 1.0f);

            // Cloud dissipation: thin cloud evaporates back to vapor
            // Rate scales with humidity deficit and pressure anomaly.
            // High pressure = subsidence = sinking warm air = fast cloud clearing.
            // Low pressure = rising air = cloud persists.
            {
                float deficit = std::max(0.0f, 1.0f - cell.q / std::max(q_sat, 0.01f));
                // Pressure anomaly: >0 for high-pressure cells (relative to 1013)
                float p_anomaly = std::clamp((cell.p - 1013.0f) * 0.1f, -0.5f, 1.0f);
                float subsidence = std::max(0.0f, p_anomaly) * CLOUD_SUBSIDE_RATE;
                float dissip_rate = CLOUD_EVAP_BASE + deficit * CLOUD_EVAP_DRY + subsidence;
                float cloud_evap = cell.cloud * dissip_rate * dt_days;
                cell.cloud -= cloud_evap;
                cell.cloud = std::max(cell.cloud, 0.0f);
                cell.q += cloud_evap;
            }

            // Rain shadow: remove moisture on leeward side of mountains
            if (uplift > 0.05f) {
                cell.q *= (1.0f - uplift * 0.2f * dt_days);
            }

            // ── Invariant 7: Convective instability release ────────────
            // When ground is much warmer than air (strong lapse rate),
            // convection triggers enhanced condensation + precipitation,
            // releasing latent heat and dumping moisture as storms.
            float ground_air_diff = cell.ground_temp - cell.T;
            if (ground_air_diff > CONVECTIVE_THRESHOLD && cell.q > q_sat * 0.5f) {
                float instability = (ground_air_diff - CONVECTIVE_THRESHOLD) * 0.1f;
                instability = std::min(instability, 0.5f);

                // Forced condensation from convective uplift
                float convective_condensed = cell.q * instability * dt_days;
                convective_condensed = std::min(convective_condensed, cell.q * 0.3f);
                cell.cloud += convective_condensed;
                cell.q -= convective_condensed;
                cell.T += convective_condensed * K_LATENT;

                // Enhanced precipitation from convective storms
                float conv_precip = cell.cloud * CONVECTIVE_PRECIP * instability * dt_days;
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

            // Storm intensity: multiplicative with thresholds — storms are rare events
            // Convergence threshold 0.2: units are day^-1 (wind divergence over 2-cell span).
            // Below this, normal large-scale circulation; above = active convergence zone.
            float humidity_ratio = (q_sat > 0.01f) ? cell.q / q_sat : 0.0f;
            float conv_excess = std::max(0.0f, convergence - 0.2f);
            float instability = std::min(conv_excess * 1.5f + T_grad * 0.05f, 1.0f);
            // Cloud boost applied before cubic so it only amplifies when instability is
            // already meaningful, preventing saturated ocean cells from bypassing rarity
            float boosted = instability * (1.0f + cell.cloud * 0.5f);
            float storm_raw = humidity_ratio * boosted * boosted * boosted;
            cell.storminess = std::clamp(storm_raw, 0.0f, 1.0f);

            // Clamp all values (Clausius-Clapeyron enforced by condensation above)
            cell.q = std::clamp(cell.q, 0.0f, 1.0f);
            cell.cloud = std::clamp(cell.cloud, 0.0f, 1.0f);
        }
    }

    // Track total atmospheric water for diagnostics
    float water_total = 0.0f;
    for (const auto& c : atmo.cells)
        water_total += c.q + c.cloud;
    atmo.total_water = water_total;
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

    double T_sum = 0, q_sum = 0, cloud_sum = 0, precip_sum = 0, wind_sum = 0, storm_sum = 0;
    double T_sum2 = 0, wind_sum2 = 0, q_sum2 = 0, precip_sum2 = 0;
    double wind_u_sum = 0, wind_v_sum = 0;  // for direction spread
    size_t n = atmo.cells.size();

    for (const auto& c : atmo.cells) {
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
    }

    auto fn = static_cast<float>(n);
    s.T_mean = static_cast<float>(T_sum / fn);
    s.q_mean = static_cast<float>(q_sum / fn);
    s.cloud_mean = static_cast<float>(cloud_sum / fn);
    s.precip_mean = static_cast<float>(precip_sum / fn);
    s.wind_mean = static_cast<float>(wind_sum / fn);
    s.storm_mean = static_cast<float>(storm_sum / fn);

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
    s.max_courant = s.wind_max * ADVECT_SPEED * atmo.last_dt_days;

    // Latitude-band diagnostics (thirds: north polar, equatorial, south polar)
    double band_T[3] = {}, band_wind[3] = {}, band_q[3] = {};
    uint32_t band_count[3] = {};
    for (uint32_t ay = 0; ay < atmo.height; ++ay) {
        int band = static_cast<int>(ay * 3 / atmo.height);
        band = std::clamp(band, 0, 2);
        for (uint32_t ax = 0; ax < atmo.width; ++ax) {
            const auto& c = atmo.cells[ay * atmo.width + ax];
            band_T[band] += c.T;
            band_q[band] += c.q;
            band_wind[band] += std::sqrt(c.u * c.u + c.v * c.v);
            ++band_count[band];
        }
    }
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

}  // namespace sandbox

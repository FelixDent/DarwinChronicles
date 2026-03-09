#include "renderer.h"

#include <algorithm>
#include <cmath>

#include "telemetry.h"

namespace {

// Draw a filled triangle using scanline fill
void fill_triangle(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int x2, int y2) {
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    if (y0 > y2) {
        std::swap(x0, x2);
        std::swap(y0, y2);
    }
    if (y1 > y2) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }

    if (y2 == y0)
        return;

    for (int y = y0; y <= y2; ++y) {
        float t_long = static_cast<float>(y - y0) / static_cast<float>(y2 - y0);
        int xa = x0 + static_cast<int>(t_long * static_cast<float>(x2 - x0));

        int xb;
        if (y < y1) {
            if (y1 == y0)
                xb = x0;
            else {
                float t = static_cast<float>(y - y0) / static_cast<float>(y1 - y0);
                xb = x0 + static_cast<int>(t * static_cast<float>(x1 - x0));
            }
        } else {
            if (y2 == y1)
                xb = x1;
            else {
                float t = static_cast<float>(y - y1) / static_cast<float>(y2 - y1);
                xb = x1 + static_cast<int>(t * static_cast<float>(x2 - x1));
            }
        }

        if (xa > xb)
            std::swap(xa, xb);
        SDL_RenderDrawLine(renderer, xa, y, xb, y);
    }
}

// Terrain color (same as worldgen)
std::array<uint8_t, 3> terrain_color(const sandbox::TerrainTile& tile) {
    if (tile.is_ocean) {
        float depth = 1.0f - tile.elev01;
        uint8_t r = static_cast<uint8_t>(10.0f + depth * 5.0f);
        uint8_t g = static_cast<uint8_t>(30.0f + depth * 30.0f);
        uint8_t b = static_cast<uint8_t>(70.0f + (1.0f - depth) * 80.0f);
        return {r, g, b};
    }
    switch (tile.band) {
        case sandbox::ElevBand::Water: {
            // Low-elevation non-ocean tile (basin)
            return {70, 140, 200};
        }
        case sandbox::ElevBand::Lowland: {
            float t = tile.elev01;
            uint8_t r = static_cast<uint8_t>(180.0f + t * 30.0f);
            uint8_t g = static_cast<uint8_t>(160.0f + t * 20.0f);
            uint8_t b = static_cast<uint8_t>(110.0f + t * 10.0f);
            return {r, g, b};
        }
        case sandbox::ElevBand::Hills: {
            float t = tile.elev01;
            uint8_t r = static_cast<uint8_t>(140.0f + t * 20.0f);
            uint8_t g = static_cast<uint8_t>(125.0f + t * 10.0f);
            uint8_t b = static_cast<uint8_t>(80.0f + t * 10.0f);
            return {r, g, b};
        }
        case sandbox::ElevBand::Mountains: {
            float t = std::clamp((tile.elev01 - 0.7f) / 0.3f, 0.0f, 1.0f);
            uint8_t v = static_cast<uint8_t>(130.0f + t * 110.0f);
            return {v, v, static_cast<uint8_t>(v + 5)};
        }
        default:
            return {100, 100, 100};
    }
}

}  // namespace

namespace sandbox {

// ── Overlay names ───────────────────────────────────────────────────────────

const char* overlay_name(OverlayMode mode) {
    switch (mode) {
        case OverlayMode::None:
            return "None";
        case OverlayMode::Temperature:
            return "Temperature";
        case OverlayMode::Precipitation:
            return "Precipitation";
        case OverlayMode::Moisture:
            return "Moisture";
        case OverlayMode::WindDirection:
            return "Wind";
        case OverlayMode::Evaporation:
            return "Evaporation";
        case OverlayMode::Storminess:
            return "Storminess";
        case OverlayMode::MoistureBars:
            return "MoistureBars";
        case OverlayMode::RainShadow:
            return "RainShadow";
        case OverlayMode::SurfaceWater:
            return "Surface Water";
        case OverlayMode::SoilMoisture:
            return "Soil Moisture";
        case OverlayMode::SnowDepth:
            return "Snow Depth";
        case OverlayMode::PrecipBudget:
            return "Precip Budget";
        case OverlayMode::UpperWind:
            return "Upper Wind";
        case OverlayMode::Stability:
            return "Stability";
        case OverlayMode::Aridity:
            return "Aridity";
        case OverlayMode::Groundwater:
            return "Groundwater";
        case OverlayMode::Discharge:
            return "Discharge";
        case OverlayMode::Geology:
            return "Geology";
        case OverlayMode::SoilTextureOverlay:
            return "Soil Texture";
        default:
            return "Unknown";
    }
}

// ── Camera ──────────────────────────────────────────────────────────────────

void Camera::pan(float dx, float dy, float dt) {
    float speed = PAN_SPEED / zoom * dt;
    x += dx * speed;
    y += dy * speed;
}

void Camera::zoom_at(float screen_x, float screen_y, float factor, int win_w, int win_h) {
    float world_x = x + (screen_x - static_cast<float>(win_w) / 2.0f) / zoom;
    float world_y = y + (screen_y - static_cast<float>(win_h) / 2.0f) / zoom;

    zoom = std::clamp(zoom * factor, MIN_ZOOM, MAX_ZOOM);

    x = world_x - (screen_x - static_cast<float>(win_w) / 2.0f) / zoom;
    y = world_y - (screen_y - static_cast<float>(win_h) / 2.0f) / zoom;
}

void Camera::center_on_world(uint32_t world_w, uint32_t world_h, int tile_size) {
    x = static_cast<float>(world_w) * static_cast<float>(tile_size) / 2.0f;
    y = static_cast<float>(world_h) * static_cast<float>(tile_size) / 2.0f;
}

void Camera::fit_world(uint32_t world_w, uint32_t world_h, int tile_size, int win_w, int win_h) {
    center_on_world(world_w, world_h, tile_size);
    float world_px_w = static_cast<float>(world_w) * static_cast<float>(tile_size);
    float world_px_h = static_cast<float>(world_h) * static_cast<float>(tile_size);
    float margin = 0.95f;  // 5% margin
    float zx = static_cast<float>(win_w) / world_px_w * margin;
    float zy = static_cast<float>(win_h) / world_px_h * margin;
    zoom = std::clamp(std::min(zx, zy), MIN_ZOOM, MAX_ZOOM);
}

void Camera::clamp_to_world(uint32_t world_w, uint32_t world_h, int tile_size, int win_w, int win_h) {
    float world_px_w = static_cast<float>(world_w) * static_cast<float>(tile_size);
    float world_px_h = static_cast<float>(world_h) * static_cast<float>(tile_size);
    float half_view_w = static_cast<float>(win_w) / (2.0f * zoom);
    float half_view_h = static_cast<float>(win_h) / (2.0f * zoom);
    // If world fits in view, center it; otherwise clamp to edges
    if (world_px_w <= half_view_w * 2.0f)
        x = world_px_w / 2.0f;
    else
        x = std::clamp(x, half_view_w, world_px_w - half_view_w);
    if (world_px_h <= half_view_h * 2.0f)
        y = world_px_h / 2.0f;
    else
        y = std::clamp(y, half_view_h, world_px_h - half_view_h);
}

SDL_Rect Camera::tile_to_screen(int tile_x, int tile_y, int tile_size, int win_w, int win_h) const {
    float world_px = static_cast<float>(tile_x * tile_size);
    float world_py = static_cast<float>(tile_y * tile_size);

    float screen_x = (world_px - x) * zoom + static_cast<float>(win_w) / 2.0f;
    float screen_y = (world_py - y) * zoom + static_cast<float>(win_h) / 2.0f;
    float size = static_cast<float>(tile_size) * zoom;

    return SDL_Rect{
        static_cast<int>(screen_x),
        static_cast<int>(screen_y),
        static_cast<int>(std::ceil(size)),
        static_cast<int>(std::ceil(size)),
    };
}

void Camera::screen_to_tile(int screen_x, int screen_y, int win_w, int win_h, int tile_size,
                            int& tile_x, int& tile_y) const {
    float world_px = x + (static_cast<float>(screen_x) - static_cast<float>(win_w) / 2.0f) / zoom;
    float world_py = y + (static_cast<float>(screen_y) - static_cast<float>(win_h) / 2.0f) / zoom;
    tile_x = static_cast<int>(std::floor(world_px / static_cast<float>(tile_size)));
    tile_y = static_cast<int>(std::floor(world_py / static_cast<float>(tile_size)));
}

// ── Renderer ────────────────────────────────────────────────────────────────

void Renderer::init(SDL_Renderer* sdl_renderer) {
    renderer_ = sdl_renderer;
}

void Renderer::shutdown() {
    renderer_ = nullptr;
}

void Renderer::render_terrain(const Terrain& world, const Camera& cam, int win_w, int win_h,
                              const DynamicState* dyn, bool dim_glyphs) {
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const TerrainTile& tile =
                world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

            auto color = terrain_color(tile);

            // Dynamic tinting: wet soil darker/greener, snow whitens
            if (dyn && !tile.is_ocean && tile.band != sandbox::ElevBand::Water) {
                const auto& dt = dyn->tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));

                // Wetness darkens and greens the ground
                float wet = std::clamp(dt.soil_moisture + dt.surface_water * 0.5f, 0.0f, 1.0f);
                color[0] = static_cast<uint8_t>(
                    std::clamp(static_cast<float>(color[0]) * (1.0f - wet * 0.25f), 0.0f, 255.0f));
                color[1] = static_cast<uint8_t>(
                    std::clamp(static_cast<float>(color[1]) * (1.0f + wet * 0.08f), 0.0f, 255.0f));
                color[2] = static_cast<uint8_t>(
                    std::clamp(static_cast<float>(color[2]) * (1.0f - wet * 0.15f), 0.0f, 255.0f));

                // Snow whitens terrain
                if (dt.snow_depth > 0.01f) {
                    float snow_t = std::clamp(dt.snow_depth * 3.0f, 0.0f, 1.0f);
                    color[0] = static_cast<uint8_t>(static_cast<float>(color[0]) * (1.0f - snow_t) +
                                                    240.0f * snow_t);
                    color[1] = static_cast<uint8_t>(static_cast<float>(color[1]) * (1.0f - snow_t) +
                                                    240.0f * snow_t);
                    color[2] = static_cast<uint8_t>(static_cast<float>(color[2]) * (1.0f - snow_t) +
                                                    255.0f * snow_t);
                }

                // Standing water tints blue
                if (dt.surface_water > 0.1f) {
                    float water_t = std::clamp((dt.surface_water - 0.1f) * 2.0f, 0.0f, 0.6f);
                    color[0] = static_cast<uint8_t>(
                        static_cast<float>(color[0]) * (1.0f - water_t) + 40.0f * water_t);
                    color[1] = static_cast<uint8_t>(
                        static_cast<float>(color[1]) * (1.0f - water_t) + 100.0f * water_t);
                    color[2] = static_cast<uint8_t>(
                        static_cast<float>(color[2]) * (1.0f - water_t) + 200.0f * water_t);
                }
            }

            SDL_SetRenderDrawColor(renderer_, color[0], color[1], color[2], 255);
            SDL_RenderFillRect(renderer_, &dst);

            // Elevation indicators on land tiles
            if (!tile.is_ocean && tile.band != sandbox::ElevBand::Water && dst.w >= 4) {
                float rough = tile.roughness;
                // When overlay active, heavily reduce glyph visibility to avoid texture noise
                float glyph_dim;
                uint8_t glyph_alpha;
                if (dim_glyphs) {
                    // Much more transparent: alpha 40-70 (was effectively 255)
                    glyph_dim = std::clamp(0.75f + cam.zoom * 0.06f, 0.78f, 0.92f);
                    glyph_alpha = static_cast<uint8_t>(std::clamp(30.0f + cam.zoom * 20.0f, 40.0f, 70.0f));
                } else {
                    glyph_dim = 0.55f;
                    glyph_alpha = 255;
                }
                uint8_t dr = static_cast<uint8_t>(color[0] * glyph_dim);
                uint8_t dg = static_cast<uint8_t>(color[1] * glyph_dim);
                uint8_t db = static_cast<uint8_t>(color[2] * glyph_dim);

                if (dim_glyphs)
                    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

                if (rough >= 0.45f) {
                    SDL_SetRenderDrawColor(renderer_, dr, dg, db, glyph_alpha);
                    int cx = dst.x + dst.w / 2;
                    int peak = dst.y + dst.h / 6;
                    int base_y = dst.y + dst.h - dst.h / 6;
                    int half_w = dst.w / 4;
                    fill_triangle(renderer_, cx, peak, cx - half_w, base_y, cx + half_w, base_y);

                    if (rough >= 0.65f) {
                        // Snow-capped peaks
                        {
                            int snow_y = peak + dst.h / 5;
                            int snow_hw = half_w / 3;
                            uint8_t snow_a = dim_glyphs ? static_cast<uint8_t>(glyph_alpha / 2) : 255;
                            SDL_SetRenderDrawColor(renderer_, 240, 240, 255, snow_a);
                            fill_triangle(renderer_, cx, peak, cx - snow_hw, snow_y, cx + snow_hw,
                                          snow_y);
                        }
                    }
                } else if (rough >= 0.15f) {
                    SDL_SetRenderDrawColor(renderer_, dr, dg, db, glyph_alpha);
                    int base_y = dst.y + dst.h - dst.h / 5;
                    int lx = dst.x + dst.w / 3;
                    int lp = dst.y + dst.h * 2 / 5;
                    int lhw = dst.w / 5;
                    fill_triangle(renderer_, lx, lp, lx - lhw, base_y, lx + lhw, base_y);
                    int rx = dst.x + dst.w * 2 / 3;
                    int rp = dst.y + dst.h / 3;
                    int rhw = dst.w / 5;
                    fill_triangle(renderer_, rx, rp, rx - rhw, base_y, rx + rhw, base_y);
                }
            }
        }
    }
}

void Renderer::render_weather_overlay(const Terrain& world, const ClimateData& climate,
                                      const Camera& cam, int win_w, int win_h, OverlayMode mode,
                                      const DynamicState* dyn, const AtmosphereState* atmo) {
    if (mode == OverlayMode::None || mode == OverlayMode::WindDirection ||
        mode == OverlayMode::MoistureBars)
        return;

    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const ClimateTile& ct =
                climate.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

            SDL_Color c;
            switch (mode) {
                case OverlayMode::Temperature: {
                    float temp_val = ct.temperature;
                    if (atmo && dyn && !dyn->paused) {
                        temp_val = atmo->sample(static_cast<uint32_t>(tx),
                                                static_cast<uint32_t>(ty), &AtmosphereCell::T);
                    }
                    c = temperature_color(temp_val);
                    break;
                }
                case OverlayMode::Precipitation: {
                    // Show dynamic precipitation from atmosphere (nearest-neighbor
                    // for sharp rain shower boundaries) or static climate fallback
                    float precip_val = ct.precipitation;
                    if (atmo && dyn && !dyn->paused) {
                        // Nearest-neighbor: rain either falls or it doesn't —
                        // bilinear smears the coarse grid into invisible gradients
                        precip_val = atmo->sample_nearest(static_cast<uint32_t>(tx),
                                                          static_cast<uint32_t>(ty),
                                                          &AtmosphereCell::precip_rate);
                        // Rescale: actual precip_rate peaks ~0.2-0.3, map to full color range
                        precip_val = std::min(precip_val * 4.0f, 1.0f);
                    }
                    c = precipitation_color(precip_val);
                    break;
                }
                case OverlayMode::Moisture: {
                    float moist_val = ct.moisture;
                    if (atmo && dyn && !dyn->paused) {
                        // Show atmospheric humidity (q) — the actual moisture in the air
                        moist_val = atmo->sample(static_cast<uint32_t>(tx),
                                                 static_cast<uint32_t>(ty), &AtmosphereCell::q);
                    }
                    c = moisture_color(moist_val);
                    break;
                }
                case OverlayMode::Evaporation: {
                    float evap_val = ct.evaporation;
                    if (dyn && !dyn->paused) {
                        const auto& dt =
                            dyn->tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                        evap_val = dt.effective_evap;
                    }
                    c = evaporation_color(evap_val);
                    break;
                }
                case OverlayMode::Storminess: {
                    float storm_val = ct.storminess;
                    if (atmo && dyn && !dyn->paused) {
                        // Nearest-neighbor: storm cells have discrete boundaries
                        storm_val = atmo->sample_nearest(static_cast<uint32_t>(tx),
                                                         static_cast<uint32_t>(ty),
                                                         &AtmosphereCell::storminess);
                    }
                    c = storminess_color(storm_val);
                    break;
                }
                case OverlayMode::PrecipBudget: {
                    if (atmo) {
                        float budget =
                            atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                         &AtmosphereCell::precip_budget);
                        c = precip_budget_color(budget);
                    } else {
                        continue;
                    }
                    break;
                }
                case OverlayMode::Stability: {
                    if (atmo) {
                        float stab =
                            atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                         &AtmosphereCell::stability);
                        c = stability_color(stab);
                    } else {
                        continue;
                    }
                    break;
                }
                case OverlayMode::UpperWind: {
                    if (atmo) {
                        float u_up =
                            atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                         &AtmosphereCell::u_upper);
                        float v_up =
                            atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                         &AtmosphereCell::v_upper);
                        float spd = std::sqrt(u_up * u_up + v_up * v_up);
                        float t = std::clamp(spd / 4.0f, 0.0f, 1.0f);
                        auto r = static_cast<uint8_t>(40.0f + t * 215.0f);
                        auto g = static_cast<uint8_t>(40.0f + t * 160.0f);
                        auto b = static_cast<uint8_t>(80.0f + t * 175.0f);
                        c = {r, g, b, 160};
                    } else {
                        continue;
                    }
                    break;
                }
                case OverlayMode::RainShadow: {
                    float shadow = ct.rain_shadow;
                    // When atmosphere is running, compute dynamic rain shadow from
                    // current wind direction + terrain: leeward of high terrain
                    // with low precip compared to windward side
                    if (atmo && dyn && !dyn->paused) {
                        auto utx = static_cast<uint32_t>(tx);
                        auto uty = static_cast<uint32_t>(ty);
                        const auto& tt = world.tile_at(utx, uty);
                        if (tt.is_ocean || tt.band == sandbox::ElevBand::Water) {
                            continue;
                        }
                        float wu = atmo->sample(utx, uty, &AtmosphereCell::u);
                        float wv = atmo->sample(utx, uty, &AtmosphereCell::v);
                        float spd = std::sqrt(wu * wu + wv * wv);
                        if (spd < 0.05f) {
                            continue;
                        }
                        // Trace upwind to find mountains
                        float udx = -wu / spd;
                        float udy = -wv / spd;
                        float fx = static_cast<float>(tx);
                        float fy = static_cast<float>(ty);
                        shadow = 0.0f;
                        for (int step = 1; step <= 12; ++step) {
                            fx += udx;
                            fy += udy;
                            int ux = static_cast<int>(std::round(fx));
                            int uy = static_cast<int>(std::round(fy));
                            if (ux < 0 || ux >= static_cast<int>(world.width) || uy < 0 ||
                                uy >= static_cast<int>(world.height))
                                break;
                            const auto& uptt =
                                world.tile_at(static_cast<uint32_t>(ux), static_cast<uint32_t>(uy));
                            float elev_diff = uptt.elev01 - tt.elev01;
                            if (elev_diff > 0.08f && uptt.elev01 > 0.45f) {
                                float dist_decay = 1.0f - static_cast<float>(step - 1) / 12.0f;
                                float s = std::clamp(elev_diff * 3.0f, 0.0f, 1.0f) * dist_decay;
                                shadow = std::max(shadow, s);
                            }
                        }
                    }
                    if (shadow > 0.05f) {
                        float t = std::clamp(shadow, 0.0f, 1.0f);
                        auto a = static_cast<uint8_t>(80.0f + t * 175.0f);
                        c = {255, static_cast<uint8_t>(160.0f * (1.0f - t * 0.6f)),
                             static_cast<uint8_t>(40.0f * (1.0f - t)), a};
                    } else {
                        continue;
                    }
                    break;
                }
                case OverlayMode::Geology: {
                    const auto& tt =
                        world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                    if (tt.is_ocean)
                        continue;
                    c = geology_color(tt.rock);
                    break;
                }
                case OverlayMode::SoilTextureOverlay: {
                    const auto& tt =
                        world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                    if (tt.is_ocean)
                        continue;
                    c = soil_texture_color(tt.soil);
                    break;
                }
                default:
                    continue;
            }

            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer_, &dst);
        }
    }
}

void Renderer::render_dynamic_overlay(const Terrain& world, const DynamicState& dyn,
                                      const Camera& cam, int win_w, int win_h, OverlayMode mode) {
    if (mode != OverlayMode::SurfaceWater && mode != OverlayMode::SoilMoisture &&
        mode != OverlayMode::SnowDepth && mode != OverlayMode::Aridity &&
        mode != OverlayMode::Groundwater && mode != OverlayMode::Discharge)
        return;

    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const auto& tt = world.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            if (tt.is_ocean || tt.band == sandbox::ElevBand::Water)
                continue;

            const auto& dt = dyn.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

            SDL_Color c;
            switch (mode) {
                case OverlayMode::SurfaceWater:
                    c = surface_water_color(dt.surface_water);
                    break;
                case OverlayMode::SoilMoisture:
                    c = soil_moisture_color(dt.soil_moisture);
                    break;
                case OverlayMode::SnowDepth:
                    c = snow_depth_color(dt.snow_depth);
                    if (c.a < 5)
                        continue;  // skip nearly invisible tiles
                    break;
                case OverlayMode::Aridity:
                    c = aridity_color(dt.aridity);
                    break;
                case OverlayMode::Groundwater:
                    c = groundwater_color(dt.groundwater);
                    break;
                case OverlayMode::Discharge:
                    c = discharge_color(dt.discharge);
                    if (c.a < 5)
                        continue;  // skip no-flow tiles
                    break;
                default:
                    continue;
            }

            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer_, &dst);
        }
    }
}

void Renderer::render_wind_arrows(const ClimateData& climate, const Camera& cam, int win_w,
                                  int win_h, const AtmosphereState* atmo) {
    // Determine arrow spacing based on zoom
    int step = std::max(1, static_cast<int>(4.0f / cam.zoom));

    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(climate.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(climate.height) - 1);

    // Align to step grid
    min_tx = (min_tx / step) * step;
    min_ty = (min_ty / step) * step;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Arrow opacity scales down when zoomed in (arrows get large and dominant)
    float zoom_alpha_scale = std::clamp(1.5f / cam.zoom, 0.4f, 1.0f);

    // Adaptive density reduction at low zoom — skip arrows to reduce clutter
    int density_step = 1;
    if (cam.zoom < 0.6f)
        density_step = 3;
    else if (cam.zoom < 1.0f)
        density_step = 2;

    // Helper: draw a single arrow (shaft + arrowhead)
    auto draw_arrow = [&](float cx, float cy, float ex, float ey, float dx, float dy,
                          float arrow_len, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        SDL_SetRenderDrawColor(renderer_, r, g, b, a);

        SDL_RenderDrawLine(renderer_, static_cast<int>(cx), static_cast<int>(cy),
                           static_cast<int>(ex), static_cast<int>(ey));

        float head_len = arrow_len * 0.35f;
        float perp_x = -dy;
        float perp_y = dx;
        float hx1 = ex - dx * head_len + perp_x * head_len * 0.5f;
        float hy1 = ey - dy * head_len + perp_y * head_len * 0.5f;
        float hx2 = ex - dx * head_len - perp_x * head_len * 0.5f;
        float hy2 = ey - dy * head_len - perp_y * head_len * 0.5f;

        SDL_RenderDrawLine(renderer_, static_cast<int>(ex), static_cast<int>(ey),
                           static_cast<int>(hx1), static_cast<int>(hy1));
        SDL_RenderDrawLine(renderer_, static_cast<int>(ex), static_cast<int>(ey),
                           static_cast<int>(hx2), static_cast<int>(hy2));
    };

    for (int ty = min_ty; ty <= max_ty; ty += step) {
        for (int tx = min_tx; tx <= max_tx; tx += step) {
            // Spatially regular density reduction at low zoom
            if (density_step > 1 && ((tx / step + ty / step) % density_step) != 0)
                continue;
            float wu, wv;
            if (atmo) {
                wu = atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                  &AtmosphereCell::u);
                wv = atmo->sample(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty),
                                  &AtmosphereCell::v);
            } else {
                const ClimateTile& ct =
                    climate.tile_at(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                wu = ct.wind_u;
                wv = ct.wind_v;
            }

            float speed = std::sqrt(wu * wu + wv * wv);
            if (speed < 0.01f)
                continue;

            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);
            float cx = static_cast<float>(dst.x) + static_cast<float>(dst.w) * 0.5f;
            float cy = static_cast<float>(dst.y) + static_cast<float>(dst.h) * 0.5f;

            float dx = wu / speed;
            float dy = wv / speed;

            // Clamp arrow length so zoomed-in arrows don't dominate
            float max_arrow = std::min(static_cast<float>(dst.w) * 0.8f, 14.0f);
            float arrow_len = max_arrow * std::min(speed, 1.5f);

            float ex = cx + dx * arrow_len;
            float ey = cy + dy * arrow_len;

            // Neutral light gray at reduced opacity — visible but doesn't compete with scalar overlay
            auto base_alpha = static_cast<uint8_t>(std::clamp(speed * 160.0f, 50.0f, 120.0f));
            auto alpha = static_cast<uint8_t>(static_cast<float>(base_alpha) * zoom_alpha_scale);

            // Dark outline for visibility against any background
            draw_arrow(cx, cy, ex, ey, dx, dy, arrow_len, 0, 0, 0,
                       static_cast<uint8_t>(static_cast<float>(alpha) * 0.4f));
            // Neutral gray foreground — magnitude encoded in length, not brightness
            draw_arrow(cx, cy, ex, ey, dx, dy, arrow_len, 215, 220, 227, alpha);
        }
    }
}

void Renderer::render_moisture_bars(const Terrain& world, const ClimateData& climate,
                                    const Camera& cam, int win_w, int win_h,
                                    const DynamicState* dyn, const AtmosphereState* /*atmo*/) {
    int min_tx, min_ty, max_tx, max_ty;
    cam.screen_to_tile(0, 0, win_w, win_h, TILE_SIZE, min_tx, min_ty);
    cam.screen_to_tile(win_w, win_h, win_w, win_h, TILE_SIZE, max_tx, max_ty);

    min_tx = std::max(min_tx - 1, 0);
    min_ty = std::max(min_ty - 1, 0);
    max_tx = std::min(max_tx + 1, static_cast<int>(world.width) - 1);
    max_ty = std::min(max_ty + 1, static_cast<int>(world.height) - 1);

    bool use_dynamic = dyn && !dyn->paused && !dyn->tiles.empty();

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            auto utx = static_cast<uint32_t>(tx);
            auto uty = static_cast<uint32_t>(ty);

            const TerrainTile& tt = world.tile_at(utx, uty);
            if (tt.is_ocean || tt.band == sandbox::ElevBand::Water)
                continue;

            float moisture;
            if (use_dynamic) {
                size_t idx = static_cast<size_t>(uty) * world.width + utx;
                moisture = dyn->tiles[idx].effective_moisture;
            } else {
                moisture = climate.tile_at(utx, uty).moisture;
            }

            if (moisture <= 0.01f)
                continue;

            SDL_Rect dst = cam.tile_to_screen(tx, ty, TILE_SIZE, win_w, win_h);

            // Wider bar: 3/4 of tile width, full height scaled by moisture
            int bar_w = std::max(2, dst.w * 3 / 4);
            int bar_h = std::max(2, static_cast<int>(moisture * static_cast<float>(dst.h)));

            SDL_Rect bar = {
                dst.x + (dst.w - bar_w) / 2,
                dst.y + dst.h - bar_h,
                bar_w,
                bar_h,
            };

            // Blue gradient: brighter for more moisture
            auto blue = static_cast<uint8_t>(150.0f + moisture * 105.0f);
            SDL_SetRenderDrawColor(renderer_, 40, 80, blue, 210);
            SDL_RenderFillRect(renderer_, &bar);
        }
    }
}

// ── UI Button (same as worldgen) ────────────────────────────────────────────

ButtonRect render_button(SDL_Renderer* renderer, int x, int y, const char* label, bool hovered) {
    constexpr int SCALE = 2;
    constexpr int PAD_X = 12;
    constexpr int PAD_Y = 8;

    int text_w = text_pixel_width(label, SCALE);
    int btn_w = text_w + PAD_X * 2;
    int btn_h = 7 * SCALE + PAD_Y * 2;

    ButtonRect rect = {x, y, btn_w, btn_h};

    if (hovered) {
        SDL_SetRenderDrawColor(renderer, 60, 80, 120, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 40, 50, 70, 255);
    }
    SDL_Rect bg_rect = {x, y, btn_w, btn_h};
    SDL_RenderFillRect(renderer, &bg_rect);

    SDL_SetRenderDrawColor(renderer, 100, 120, 160, 255);
    SDL_RenderDrawRect(renderer, &bg_rect);

    draw_text(renderer, x + PAD_X, y + PAD_Y, label, SCALE, 220, 220, 240);

    return rect;
}

ButtonRect render_status_chip(SDL_Renderer* renderer, int x, int y, const char* label,
                              bool hovered) {
    constexpr int SCALE = 2;
    constexpr int PAD_X = 12;
    constexpr int PAD_Y = 8;

    int text_w = text_pixel_width(label, SCALE);
    int btn_w = text_w + PAD_X * 2;
    int btn_h = 7 * SCALE + PAD_Y * 2;

    ButtonRect rect = {x, y, btn_w, btn_h};

    // Status chip: darker, distinct from action buttons
    if (hovered) {
        SDL_SetRenderDrawColor(renderer, 30, 42, 58, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 26, 36, 50, 255);
    }
    SDL_Rect bg_rect = {x, y, btn_w, btn_h};
    SDL_RenderFillRect(renderer, &bg_rect);

    SDL_SetRenderDrawColor(renderer, 43, 58, 82, 255);
    SDL_RenderDrawRect(renderer, &bg_rect);

    // Lighter blue-white text to distinguish from action buttons
    draw_text(renderer, x + PAD_X, y + PAD_Y, label, SCALE, 207, 227, 255);

    return rect;
}

}  // namespace sandbox

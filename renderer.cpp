// renderer.cpp
#include "renderer.hpp"
#include "app.hpp"
#include "simulation.hpp"
#include <imgui.h>
#include <imgui_impl_sdlrenderer3.h>

void DrawWarehouse(App* app, Warehouse* wh, float view_w, float view_h, float alpha) {
    VertexBuffer* vb = &app->vertices;
    IndexBuffer* ib = &app->indices;
    vb->Clear();
    ib->Clear();

    const LiveConfig* live = &app->world.live;
    Camera* camera = &app->camera;
    Controller* controller = wh->controller;

    ViewXform xf;
    xf.cell_px = CellWorld * camera->zoom;
    xf.origin_x = view_w * 0.5f - camera->x * camera->zoom;
    xf.origin_y = view_h * 0.5f - camera->y * camera->zoom;

    const int32_t cols = wh->cols;
    const int32_t rows = wh->rows;
    const float px = xf.cell_px;
    const bool overlay = live->debug_overlay;

    uint8_t station_active[MaxGridCols] = {};
    for (int32_t b = 0; b < wh->bot_count; b++) {
        const Bot* bot = &wh->bots[b];
        if (bot->state == BotState::Dropping) station_active[CellCol(bot->cell, cols)] = 1;
    }

    float inset = sdl::max(RENDER_TILE_INSET_MIN, px * RENDER_TILE_INSET_RATIO);
    SDL_FColor tile_a = MakeColor(0.135f, 0.155f, 0.205f);
    SDL_FColor tile_b = MakeColor(0.150f, 0.172f, 0.225f);
    SDL_FColor station_tile = MakeColor(0.105f, 0.255f, 0.235f);
    SDL_FColor heat_color = MakeColor(0.95f, 0.30f, 0.22f);

    for (int32_t row = 0; row < rows; row++) {
        for (int32_t col = 0; col < cols; col++) {
            SDL_FPoint corner = xf.Corner((float)col, (float)row);
            if (corner.x > view_w || corner.y > view_h || corner.x + px < 0.0f || corner.y + px < 0.0f) continue;

            SDL_FColor color = ((col + row) & 1) ? tile_a : tile_b;
            if (row == rows - 1) color = station_tile;
            if (overlay) {
                float heat = controller->HeatAt(wh, CellIndex(col, row, cols));
                if (heat > 0.0f) {
                    float area = (float)((live->crowd_radius * 2 + 1) * (live->crowd_radius * 2 + 1));
                    color = MixColor(color, heat_color, sdl::saturate(heat / (area * 0.36f) * 0.75f));
                }
            }
            PushRect(vb, ib, corner.x + inset, corner.y + inset, px - inset * 2.0f, px - inset * 2.0f, color);
        }
    }

    for (int32_t col = 0; col < cols; col++) {
        SDL_FPoint corner = xf.Corner((float)col, (float)rows);
        if (corner.x > view_w || corner.x + px < 0.0f) continue;
        SDL_FColor bay = station_active[col] ? MakeColor(0.30f, 0.80f, 0.62f) : MakeColor(0.085f, 0.215f, 0.195f);
        PushRect(vb, ib, corner.x + px * RENDER_BAY_X_OFFSET, corner.y + px * RENDER_BAY_Y_OFFSET, px * RENDER_BAY_WIDTH, px * RENDER_BAY_HEIGHT, bay);
    }

    if (overlay) {
        for (int32_t row = 0; row < rows; row++) {
            float bias = controller->LaneBias(wh, true, row);
            if (sdl::fabsf(bias) < 0.15f) continue;
            SDL_FPoint c = xf.Center(-0.62f, (float)row);
            float s = px * 0.20f;
            float dir = bias > 0.0f ? 1.0f : -1.0f;
            SDL_FColor color = bias > 0.0f ? MakeColor(0.35f, 0.65f, 1.0f, sdl::saturate(sdl::fabsf(bias) + 0.2f)) : MakeColor(1.0f, 0.62f, 0.25f, sdl::saturate(sdl::fabsf(bias) + 0.2f));
            PushTriangle(vb, ib, SDL_FPoint{ c.x + s * dir, c.y }, SDL_FPoint{ c.x - s * dir, c.y - s }, SDL_FPoint{ c.x - s * dir, c.y + s }, color);
        }
        for (int32_t col = 0; col < cols; col++) {
            float bias = controller->LaneBias(wh, false, col);
            if (sdl::fabsf(bias) < 0.15f) continue;
            SDL_FPoint c = xf.Center((float)col, -0.62f);
            float s = px * 0.20f;
            float dir = bias > 0.0f ? 1.0f : -1.0f; 
            SDL_FColor color = bias > 0.0f ? MakeColor(0.35f, 0.65f, 1.0f, sdl::saturate(sdl::fabsf(bias) + 0.2f)) : MakeColor(1.0f, 0.62f, 0.25f, sdl::saturate(sdl::fabsf(bias) + 0.2f));
            PushTriangle(vb, ib, SDL_FPoint{ c.x, c.y + s * dir }, SDL_FPoint{ c.x - s, c.y - s * dir }, SDL_FPoint{ c.x + s, c.y - s * dir }, color);
        }
    }

    for (int32_t o = 0; o < wh->next_release; o++) {
        OrderState state = wh->orders[o].state;
        if (state != OrderState::Available && state != OrderState::Claimed) continue;
        const OrderSpec* spec = &wh->scenario->orders[o];
        SDL_FPoint c = xf.Center((float)CellCol(spec->pick_cell, cols), (float)CellRow(spec->pick_cell, cols));
        if (c.x < -px || c.y < -px || c.x > view_w + px || c.y > view_h + px) continue;

        SDL_FColor color = RgbToFColor(colors::DistinctColors[spec->color_index]);
        float half = px * RENDER_CRATE_HALF_SIZE;
        PushRect(vb, ib, c.x - half, c.y - half, half * 2.0f, half * 2.0f, color);
        if (state == OrderState::Claimed) {
            float ring = half + px * RENDER_CRATE_RING_OFFSET;
            PushRectOutline(vb, ib, c.x - ring, c.y - ring, ring * 2.0f, ring * 2.0f, sdl::max(1.0f, px * 0.035f), MakeColor(1.0f, 1.0f, 1.0f, 0.85f));
        }
    }

    const int32_t selected_id = app->world.selected_bot_plus1 - 1;

    if (live->show_trails) {
        for (int32_t b = 0; b < wh->bot_count; b++) {
            const Bot* bot = &wh->bots[b];
            if (bot->trail_count < 2) continue;
            SDL_FColor base = RgbToFColor(colors::DistinctColors[wh->scenario->bot_color[b]]);
            float half_thickness = sdl::max(RENDER_TRAIL_THICKNESS_MIN, px * RENDER_TRAIL_THICKNESS_RATIO);
            for (int32_t k = 0; k < bot->trail_count - 1; k++) {
                int32_t i0 = (bot->trail_head - bot->trail_count + k + TrailLength * 2) % TrailLength;
                int32_t i1 = (i0 + 1) % TrailLength;
                float a0 = 0.50f * (float)k / (float)bot->trail_count;
                float a1 = 0.50f * (float)(k + 1) / (float)bot->trail_count;
                SDL_FPoint p0 = xf.Center(bot->trail[i0].X, bot->trail[i0].Y);
                SDL_FPoint p1 = xf.Center(bot->trail[i1].X, bot->trail[i1].Y);
                PushThickLine(vb, ib, p0, p1, half_thickness, SDL_FColor{ base.r, base.g, base.b, a0 }, SDL_FColor{ base.r, base.g, base.b, a1 });
            }
        }
    }

    for (int32_t b = 0; b < wh->bot_count; b++) {
        const Bot* bot = &wh->bots[b];
        bool is_selected = b == selected_id;
        if (!is_selected && !live->show_paths) continue;
        if (bot->path_length < 2 || bot->path_index + 1 >= bot->path_length) continue;

        Vec2 here = sdl::lerp(bot->prev_pos, bot->pos, alpha);
        SDL_FPoint previous = xf.Center(here.X, here.Y);
        SDL_FColor color = is_selected ? MakeColor(1.0f, 1.0f, 1.0f, 0.85f) : MakeColor(0.7f, 0.8f, 1.0f, 0.22f);
        float half_thickness = sdl::max(0.7f, px * (is_selected ? RENDER_PATH_THICKNESS_SELECTED : RENDER_PATH_THICKNESS_NORMAL));
        for (int32_t k = bot->path_index + 1; k < bot->path_length; k++) {
            SDL_FPoint next = xf.Center((float)CellCol(bot->path[k], cols), (float)CellRow(bot->path[k], cols));
            PushThickLine(vb, ib, previous, next, half_thickness, color, color);
            previous = next;
        }
        if (is_selected && bot->has_target) {
            SDL_FPoint t = xf.Center((float)CellCol(bot->target_cell, cols), (float)CellRow(bot->target_cell, cols));
            PushArcFan(vb, ib, t, px * 0.16f, 0.0f, 1.0f, MakeColor(1.0f, 1.0f, 1.0f, 0.9f), 12);
        }
        if (is_selected) {
            for (int32_t i = 0; i < MaxBlockedMemory; i++) {
                if (!(wh->time < bot->blocked_until[i])) continue;
                SDL_FPoint c = xf.Center((float)CellCol(bot->blocked_cell[i], cols), (float)CellRow(bot->blocked_cell[i], cols));
                float half = px * 0.40f;
                PushRectOutline(vb, ib, c.x - half, c.y - half, half * 2.0f, half * 2.0f, sdl::max(1.0f, px * 0.05f), MakeColor(1.0f, 0.25f, 0.25f, 0.9f));
            }
        }
    }

    for (int32_t b = 0; b < wh->bot_count; b++) {
        const Bot* bot = &wh->bots[b];
        Vec2 here = sdl::lerp(bot->prev_pos, bot->pos, alpha);
        SDL_FPoint c = xf.Center(here.X, here.Y);
        if (c.x < -px || c.y < -px || c.x > view_w + px || c.y > view_h + px) continue;

        float turns = bot->heading_turns;
        float fx = sdl::cos_turns(turns), fy = sdl::sin_turns(turns);

        PushRotatedRect(vb, ib, c, px * RENDER_BOT_BODY_HALF, px * RENDER_BOT_BODY_HALF, turns, BotStateColor(wh, bot));
        PushRotatedRect(vb, ib, c, px * RENDER_BOT_LID_HALF, px * RENDER_BOT_LID_HALF, turns, RgbToFColor(colors::DistinctColors[wh->scenario->bot_color[b]]));
        PushTriangle(vb, ib,
            SDL_FPoint{ c.x + fx * px * RENDER_BOT_NOSE_TIP_X, c.y + fy * px * RENDER_BOT_NOSE_TIP_Y },
            SDL_FPoint{ c.x + fx * px * RENDER_BOT_NOSE_BASE_X - fy * px * RENDER_BOT_NOSE_BASE_Y, c.y + fy * px * RENDER_BOT_NOSE_BASE_X + fx * px * RENDER_BOT_NOSE_BASE_Y },
            SDL_FPoint{ c.x + fx * px * RENDER_BOT_NOSE_BASE_X + fy * px * RENDER_BOT_NOSE_BASE_Y, c.y + fy * px * RENDER_BOT_NOSE_BASE_X - fx * px * RENDER_BOT_NOSE_BASE_Y },
            MakeColor(0.06f, 0.07f, 0.10f, 0.85f));

        for (int32_t i = 0; i < bot->cargo_count; i++) {
            const OrderSpec* spec = &wh->scenario->orders[bot->cargo[i] - 1];
            float along = RENDER_CARGO_OFFSET_START + RENDER_CARGO_OFFSET_STEP * (float)i;
            SDL_FPoint p = { c.x + fx * px * along, c.y + fy * px * along };
            float half = px * RENDER_CARGO_HALF_SIZE;
            PushRotatedRect(vb, ib, p, half, half, turns, RgbToFColor(colors::DistinctColors[spec->color_index]));
        }

        if (b == selected_id) {
            float half = px * RENDER_SELECTION_OUTLINE_HALF;
            PushRectOutline(vb, ib, c.x - half, c.y - half, half * 2.0f, half * 2.0f, sdl::max(1.2f, px * 0.05f), MakeColor(1.0f, 1.0f, 1.0f, 0.95f));
        }
    }

    app->renderer.SetDrawBlendMode(SDL_BLENDMODE_BLEND);
    if (vb->Count() > 0 && ib->Count() > 0) app->renderer.DrawGeometry(vb->Data(), vb->Count(), ib->Data(), ib->Count());

    if (selected_id >= 0 && selected_id < wh->bot_count) {
        const Bot* bot = &wh->bots[selected_id];
        Vec2 here = sdl::lerp(bot->prev_pos, bot->pos, alpha);
        app->renderer.SetDrawColor(255, 255, 255, 220);
        PushRingOutline(app->renderer.handle, xf.Center(here.X, here.Y), px * RENDER_SELECTION_RING_RADIUS, 40);
    }
}

void RenderAll(App* app, float alpha, float delta_seconds) {
    ImGui::Render();

    sdl::Renderer* renderer = &app->renderer;
    float density = app->window.PixelDensity();
    app->camera.UpdateZoom(delta_seconds);

    for (int32_t v = 0; v < app->view_count; v++) {
        ViewRect rect = app->view_rect[v];
        sdl::RenderTarget* target = &app->view_target[v];

        target->Ensure(renderer, (int32_t)rect.w, (int32_t)rect.h, density, SupersampleFactor);
        if (!target->handle) continue;

        renderer->SetTarget(target->handle);
        renderer->SetScale(density * (float)SupersampleFactor, density * (float)SupersampleFactor);
        renderer->SetDrawBlendMode(SDL_BLENDMODE_NONE);
        renderer->SetDrawColor(10, 12, 17, 255);
        renderer->Clear();

        DrawWarehouse(app, app->world.sims[(int32_t)app->view_side[v]], rect.w, rect.h, alpha);
    }

    renderer->SetTarget(nullptr);
    renderer->SetScale(density, density);
    renderer->SetDrawBlendMode(SDL_BLENDMODE_NONE);
    renderer->SetDrawColor(10, 12, 17, 255);
    renderer->Clear();
    for (int32_t v = 0; v < app->view_count; v++) {
        sdl::RenderTarget* target = &app->view_target[v];
        if (!target->handle) continue;
        ViewRect rect = app->view_rect[v];
        renderer->DrawTextureRect(target->handle, rect.x, rect.y, (float)target->logical_width, (float)target->logical_height);

    }
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer->handle);
}

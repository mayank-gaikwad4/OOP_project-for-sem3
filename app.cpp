// app.cpp
#include "app.hpp"
#include "simulation.hpp"

ImGuiStyle g_base_ui_style;

void UpdateMainCallbackRate(App* app) {
    double sim_hz = (double)app->world.live.sim_steps_per_second;
    double target_hz = sim_hz;
    if (app->refresh_rate_hz > target_hz) target_hz = (double)app->refresh_rate_hz;

    char rate_text[32];
    sdl::snprintf(rate_text, sizeof(rate_text), "%.4f", target_hz);
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, rate_text);
}

void ApplySimRate(App* app) {
    int32_t steps = sdl::clamp(app->world.live.sim_steps_per_second, MinSimStepsPerSecond, MaxSimStepsPerSecond);
    app->world.live.sim_steps_per_second = steps;
    app->tick_period_ns = SDL_UINT64_C(1'000'000'000) / (uint64_t)steps;
    UpdateMainCallbackRate(app);
}

void ComputeLayout(App* app) {
    int32_t window_width, window_height;
    app->window.GetSize(&window_width, &window_height);

    float ui = app->world.live.ui_scale;
    if (!app->show_sidebar) {
        app->sidebar_width = 0.0f;
    }
    else {
        float sidebar = sdl::clamp(app->desired_sidebar_width * ui, 260.0f * ui, (float)window_width * 0.5f);
        app->sidebar_width = sdl::floorf(sidebar);
    }

    float available = (float)window_width - app->sidebar_width;
    if (available < 64.0f) 
        available = 64.0f;

    if (app->view_mode == ViewMode::Split) {
        app->view_count = 2;
        app->view_side[0] = SimSide::Baseline;
        app->view_side[1] = SimSide::Flagship;
        float each = sdl::floorf((available - ViewGap) * 0.5f);
        for (int32_t v = 0; v < 2; v++) {
            app->view_rect[v] = ViewRect{ app->sidebar_width + (each + ViewGap) * (float)v, 0.0f, each, (float)window_height };
        }
    } else {
        app->view_count = 1;
        app->view_side[0] = (app->view_mode == ViewMode::BaselineOnly) ? SimSide::Baseline : SimSide::Flagship;
        app->view_rect[0] = ViewRect{ app->sidebar_width, 0.0f, sdl::floorf(available), (float)window_height };
    }
}

int32_t ViewIndexAt(const App* app, float x, float y) {
    for (int32_t v = 0; v < app->view_count; v++) {
        const ViewRect* r = &app->view_rect[v];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
            return v;
    }
    return -1;
}

void FitCamera(App* app) {
    ComputeLayout(app);
    float ui = app->world.live.ui_scale;
    float hud = HudHeight * ui;

    float smallest_w = app->view_rect[0].w;
    float smallest_h = app->view_rect[0].h;
    for (int32_t v = 1; v < app->view_count; v++) {
        smallest_w = sdl::min(smallest_w, app->view_rect[v].w);
        smallest_h = sdl::min(smallest_h, app->view_rect[v].h);
    }

    float grid_w = (float)app->world.scenario->cols * CellWorld;
    float grid_h = ((float)app->world.scenario->rows + 1.6f) * CellWorld; 
    float zoom_x = (smallest_w - 24.0f) / grid_w;
    float zoom_y = (smallest_h - hud - 24.0f) / grid_h;
    float zoom = sdl::clamp(sdl::min(zoom_x, zoom_y), 0.04f, 15.0f);

    app->camera_auto_fit = true;
    app->camera.zoom = zoom;
    app->camera.zoom_target = zoom;
    app->camera.x = grid_w * 0.5f;

    float view_h = app->view_rect[0].h;
    float grid_px = grid_h * zoom;
    float top_margin = 18.0f * ui;
    if (grid_px < view_h - hud - top_margin * 2.0f) {
        app->camera.y = (view_h * 0.5f - (hud + top_margin)) / zoom;
    } else {
        app->camera.y = grid_h * 0.5f - (hud * 0.5f) / zoom;
    }
}

void RefitIfLayoutChanged(App* app) {
    int32_t width, height;
    app->window.GetSize(&width, &height);
    bool changed = width != app->last_fit_width || height != app->last_fit_height ||
                   app->sidebar_width != app->last_fit_sidebar || app->view_mode != app->last_fit_mode;
    app->last_fit_width = width;
    app->last_fit_height = height;
    app->last_fit_sidebar = app->sidebar_width;
    app->last_fit_mode = app->view_mode;
    if (changed && app->camera_auto_fit) FitCamera(app);
}

void AppRebuild(App* app) {
    app->world.restart_cfg = app->pending_restart_cfg;
    RebuildWorld(&app->world);
    app->pending_restart_cfg = app->world.restart_cfg; 

    FitCamera(app);
    ApplySimRate(app);
    app->sim_accumulator_ns = 0;
    app->render_accumulator_ns = 0;
    app->race_winner_announced = false;

    uint64_t now_ns = sdl::now_ns();
    app->last_iteration_ns = now_ns;
    app->last_render_ns = now_ns;
    app->stats_window_start_ns = now_ns;
}

Bot* FindBotNearWorldPoint(Warehouse* wh, Vec2 world_point, float max_distance) {
    Bot* best = nullptr;
    float best_distance = max_distance;
    for (int32_t b = 0; b < wh->bot_count; b++) {
        Bot* bot = &wh->bots[b];
        if (bot->retired) 
            continue;
        Vec2 bot_world((bot->pos.X + 0.5f) * CellWorld, (bot->pos.Y + 0.5f) * CellWorld);
        float d = Distance2D(bot_world, world_point);
        if (d < best_distance) {
            best_distance = d;
            best = bot;
        }
    }
    return best;
}

void UpdateUiScale(App* app, float delta_seconds) {
    float current = app->world.live.ui_scale;
    float next = sdl::lerp_exp(current, app->ui_scale_target, UI_SCALE_GLIDE_RATE, delta_seconds);
    app->world.live.ui_scale = next;

    ImGuiStyle style = g_base_ui_style;
    style.ScaleAllSizes(next);
#if IMGUI_VERSION_NUM >= 19200
    style.FontScaleMain = next;     
    ImGui::GetStyle() = style;
#else
    ImGui::GetStyle() = style;
    ImGui::GetIO().FontGlobalScale = next;
#endif
}

void CheckRaceWinner(App* app) {
    World* world = &app->world;
    if (app->race_winner_announced) 
        return;
    if (world->winner == RaceWinner::None) 
        return;

    app->race_winner_announced = true;
    if (world->winner == RaceWinner::Baseline) 
        app->leaderboard_wins[(int32_t)SimSide::Baseline]++;
    else if (world->winner == RaceWinner::Flagship) 
        app->leaderboard_wins[(int32_t)SimSide::Flagship]++;
    else app->leaderboard_ties++;
    ImGui::OpenPopup("Race Winner!");
}

inline ImU32 SideAccent(SimSide side) { return side == SimSide::Baseline ? AccentBaseline : AccentFlagship; }

float WarehouseClock(const Warehouse* wh) { return wh->finished ? wh->finish_time : wh->time; }

float OrdersPerMinute(const Warehouse* wh) {
    float t = WarehouseClock(wh);
    if (t < 0.5f) return 0.0f;
    return (float)wh->delivered / t * 60.0f;
}

float AverageLatency(const Warehouse* wh) {
    if (wh->delivered == 0) return 0.0f;
    return wh->stats.latency_sum / (float)wh->delivered;
}

float WaitFraction(const Warehouse* wh) {
    float bot_seconds = WarehouseClock(wh) * (float)wh->bot_count;
    if (bot_seconds < 0.5f) return 0.0f;
    return sdl::saturate(wh->stats.wait_seconds / bot_seconds);
}

void HelpText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void WrappedColoredText(ImVec4 color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void ResultRow(const char* label, const char* format, float baseline_value, float flagship_value, bool lower_is_better) {
    char text_a[48], text_b[48];
    sdl::snprintf(text_a, sizeof(text_a), format, baseline_value);
    sdl::snprintf(text_b, sizeof(text_b), format, flagship_value);

    bool a_better = lower_is_better ? baseline_value < flagship_value : baseline_value > flagship_value;
    bool b_better = lower_is_better ? flagship_value < baseline_value : flagship_value > baseline_value;
    ImVec4 good(0.45f, 0.95f, 0.55f, 1.0f);
    ImVec4 plain(0.85f, 0.87f, 0.92f, 1.0f);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1); ImGui::TextColored(a_better ? good : plain, "%s", text_a);
    ImGui::TableSetColumnIndex(2); ImGui::TextColored(b_better ? good : plain, "%s", text_b);
}

void RunPanel::Draw(App* app) {
    LiveConfig* live = &app->world.live;

    if (ImGui::Button(app->paused ? "Play  [P]" : "Pause [P]")) app->paused = !app->paused;
    ImGui::SameLine();
    if (ImGui::Button("Step [N]")) { app->paused = true; app->step_once = true; }
    ImGui::SameLine();
    if (ImGui::Button("Restart [R]")) AppRebuild(app);
    ImGui::SameLine();
    if (ImGui::Button("Fit [F]")) FitCamera(app);

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##speed", &live->sim_speed, 0.1f, 64.0f, "speed %.1fx", ImGuiSliderFlags_Logarithmic);
    static const float presets[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };
    static const char* preset_names[] = { "1x", "2x", "4x", "8x", "16x", "32x", "64x" };
    for (int32_t i = 0; i < 7; i++) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::SmallButton(preset_names[i])) live->sim_speed = presets[i];
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("View");
    int32_t mode = (int32_t)app->view_mode;
    bool changed = false;
    changed |= ImGui::RadioButton("Split [1]", &mode, (int32_t)ViewMode::Split); ImGui::SameLine();
    changed |= ImGui::RadioButton("Baseline [2]", &mode, (int32_t)ViewMode::BaselineOnly); ImGui::SameLine();
    changed |= ImGui::RadioButton("Flagship [3]", &mode, (int32_t)ViewMode::FlagshipOnly);
    if (changed) {
        app->view_mode = (ViewMode)mode;
        FitCamera(app);
    }

    HelpText("Both fleets always run the same order list in lockstep - the view only chooses what you watch.");
}

void ResultsPanel::Draw(App* app) {
    World* world = &app->world;
    Warehouse* base = world->sims[(int32_t)SimSide::Baseline];
    Warehouse* flag = world->sims[(int32_t)SimSide::Flagship];

    ImGui::Text("First to %d orders   |   seed %llu", world->scenario->order_count, (unsigned long long)world->restart_cfg.seed);

    if (ImGui::BeginTable("results", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Baseline", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Flagship", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        ResultRow("Time (s)", "%.1f", WarehouseClock(base), WarehouseClock(flag), true);
        ResultRow("Delivered", "%.0f", (float)base->delivered, (float)flag->delivered, false);
        ResultRow("Orders / min", "%.1f", OrdersPerMinute(base), OrdersPerMinute(flag), false);
        ResultRow("Avg order latency (s)", "%.1f", AverageLatency(base), AverageLatency(flag), true);
        ResultRow("Bot time waiting", "%.0f%%", WaitFraction(base) * 100.0f, WaitFraction(flag) * 100.0f, true);
        ResultRow("Wait ticks", "%.0f", (float)base->stats.yield_ticks, (float)flag->stats.yield_ticks, true);
        ResultRow("A* reroutes", "%.0f", (float)base->stats.reroutes, (float)flag->stats.reroutes, true);
        ResultRow("Instant sidesteps", "%.0f", (float)base->stats.sidesteps, (float)flag->stats.sidesteps, false);
        ResultRow("Deadlock breaks", "%.0f", (float)base->stats.unstick_moves, (float)flag->stats.unstick_moves, true);
        ResultRow("On-the-way pickups", "%.0f", (float)base->stats.on_the_way_pickups, (float)flag->stats.on_the_way_pickups, false);
        ResultRow("Distance (cells)", "%.0f", base->stats.distance_cells, flag->stats.distance_cells, true);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Leaderboard   Baseline %d   |   Flagship %d   |   Ties %d",
        app->leaderboard_wins[(int32_t)SimSide::Baseline], app->leaderboard_wins[(int32_t)SimSide::Flagship], app->leaderboard_ties);
    if (ImGui::SmallButton("Reset leaderboard")) app->ResetLeaderboard();
}

void ScenarioPanel::Draw(App* app) {
    RestartConfig* cfg = &app->pending_restart_cfg;
    bool grid_edit_done = false;

    // Changed DragInt to SliderInt and removed the speed parameter
    ImGui::SliderInt("Columns", &cfg->cols, 4, MaxGridCols, "%d", ImGuiSliderFlags_AlwaysClamp);
    grid_edit_done |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderInt("Rows", &cfg->rows, 4, MaxGridRows, "%d", ImGuiSliderFlags_AlwaysClamp);
    grid_edit_done |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::Checkbox("Rebuild as soon as the grid size changes", &app->rebuild_on_grid_edit);

    ImGui::SliderInt("Bots", &cfg->bot_count, 1, MaxBots, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Orders to win", &cfg->orders_to_win, 1, MaxOrders, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Orders waiting at t=0", &cfg->initial_backlog, 0, MaxOrders, "%d", ImGuiSliderFlags_AlwaysClamp);

    ImGui::SliderFloat("New orders / s", &cfg->order_rate, 0.1f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::InputScalar("Seed (0 = random)", ImGuiDataType_U64, &cfg->seed);

    // ... rest of the function stays exactly the same
    ClampRestartConfig(cfg);

    if (ImGui::Button("Apply & Restart")) AppRebuild(app);
    ImGui::SameLine();
    if (ImGui::Button("New random seed")) { cfg->seed = 0; AppRebuild(app); }

    if (grid_edit_done && app->rebuild_on_grid_edit) AppRebuild(app);

    HelpText("The order list, crate positions, station targets and bot starts come from the seed, once, and are shared by both fleets.");
}

void KinematicsPanel::Draw(App* app) {
    LiveConfig* live = &app->world.live;
    ImGui::SliderFloat("Max speed (cells/s)", &live->max_speed, 0.5f, 12.0f, "%.1f");
    ImGui::SliderFloat("Acceleration", &live->accel, 0.5f, 30.0f, "%.1f");
    ImGui::SliderFloat("Braking", &live->decel, 0.5f, 40.0f, "%.1f");
    ImGui::SliderFloat("Corner speed limit", &live->turn_speed, 0.1f, live->max_speed, "%.1f");
    HelpText("A long straight lets a bot reach max speed; every corner forces it down to the corner limit first.");

    ImGui::Spacing();
    ImGui::SliderFloat("Pick-up time (s)", &live->pickup_seconds, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Drop-off time (s)", &live->dropoff_seconds, 0.0f, 5.0f, "%.2f");

    ImGui::Spacing();
    ImGui::SliderFloat("Body clears cell at", &live->body_clear_progress, 0.5f, 1.0f, "%.2f of step");
    ImGui::SliderFloat("Extra cell hold (s)", &live->clear_hold_seconds, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Reservation look-ahead margin", &live->reserve_margin, 0.0f, 1.0f, "%.2f cells");
    HelpText("A cell stays blocked from reservation until the bot's body has left it, plus the extra hold.");
}

void TaskingPanel::Draw(App* app) {
    LiveConfig* live = &app->world.live;

    static const char* policies[] = { "Nearest order first", "Oldest order first" };
    int32_t policy = (int32_t)live->assign_policy;
    if (ImGui::Combo("Order assignment", &policy, policies, 2)) live->assign_policy = (AssignPolicy)policy;
    ImGui::SliderInt("Bot capacity", &live->bot_capacity, 1, MaxCargo);
    ImGui::Checkbox("Pick up orders on the way", &live->pickup_on_the_way);
    ImGui::Checkbox("Idle bots leave the grid when no orders remain", &live->retire_idle_bots);
    ImGui::SliderInt("Max detour (cells)", &live->pickup_detour, 0, 3);

    ImGui::Spacing();
    ImGui::TextUnformatted("Yield & reroute (both fleets)");
    ImGui::SliderFloat("Wait tick (s)", &live->yield_tick_seconds, 0.05f, 1.0f, "%.2f");
    ImGui::SliderInt("Reroute when wait ticks >", &live->wait_tick_limit, 0, 10);
    ImGui::SliderFloat("Blocked-cell memory (s)", &live->blocked_memory_seconds, 0.5f, 20.0f, "%.1f");
}

void FlagshipPanel::Draw(App* app) {
    LiveConfig* live = &app->world.live;

    ImGui::Checkbox("Separation: congestion avoidance", &live->use_congestion);
    ImGui::SliderFloat("  congestion weight", &live->congestion_weight, 0.0f, 4.0f, "%.2f");

    ImGui::SliderInt("  crowd radius", &live->crowd_radius, 1, 5, "%d cells");
    ImGui::Checkbox("  auto crowd trigger", &live->auto_crowd_trigger);
    if (!live->auto_crowd_trigger) {
        int32_t area = (live->crowd_radius * 2 + 1) * (live->crowd_radius * 2 + 1);
        ImGui::SliderInt("  dodge when crowd >=", &live->crowd_trigger, 1, area - 1);
    }

    ImGui::Checkbox("  instant sidestep to a free parallel rail", &live->use_sidestep);


    ImGui::Spacing();
    ImGui::Checkbox("Alignment: lane polarization", &live->use_lanes);
    ImGui::SliderFloat("  lane weight", &live->lane_weight, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("  lane memory (s)", &live->lane_memory_seconds, 1.0f, 30.0f, "%.1f");

    ImGui::Spacing();
    ImGui::Checkbox("Cohesion: intersection platooning", &live->use_platoons);
    ImGui::SliderFloat("  follower may enter at", &live->platoon_follow_progress, 0.4f, 1.0f, "%.2f of leader's step");

    ImGui::Spacing();
    ImGui::SliderFloat("Corner penalty (A* cost)", &live->turn_penalty, 0.0f, 3.0f, "%.2f");
}

void DisplayPanel::Draw(App* app) {
    LiveConfig* live = &app->world.live;
    ImGui::Checkbox("Heat & lane overlay [D]", &live->debug_overlay);
    ImGui::Checkbox("Bot trails [T]", &live->show_trails);
    ImGui::Checkbox("All planned routes [O]", &live->show_paths);
    HelpText("Overlay: red = crowded 3x3 blocks, arrows on the edges = lane direction (blue east/south, orange west/north).");

    ImGui::Spacing();
    if (ImGui::SliderInt("Sim steps / s", &live->sim_steps_per_second, MinSimStepsPerSecond, MaxSimStepsPerSecond)) ApplySimRate(app);
    ImGui::Text("UI scale %.2fx  (Ctrl +/-)", app->ui_scale_target);
    ImGui::Text("%.0f fps   %.0f ticks/s", app->measured_fps, app->measured_tick_rate);

    ImGui::Spacing();
    HelpText("Left click: inspect a bot   Right drag: pan   Wheel: zoom   Esc: deselect");
    HelpText("Bot colours: grey moving, amber/red yielding, green picking, blue dropping.");
}

void InitUiPanels(App* app) {
    app->panel_count = 0;
    app->panels[app->panel_count++] = PushObject<RunPanel>(app->permanent_arena);
    app->panels[app->panel_count++] = PushObject<ResultsPanel>(app->permanent_arena);
    app->panels[app->panel_count++] = PushObject<ScenarioPanel>(app->permanent_arena);
    app->panels[app->panel_count++] = PushObject<KinematicsPanel>(app->permanent_arena);
    app->panels[app->panel_count++] = PushObject<TaskingPanel>(app->permanent_arena);
    app->panels[app->panel_count++] = PushObject<FlagshipPanel>(app->permanent_arena);
    app->panels[app->panel_count++] = PushObject<DisplayPanel>(app->permanent_arena);
}

void DrawViewHud(App* app, int32_t view_index) {
    ViewRect rect = app->view_rect[view_index];
    SimSide side = app->view_side[view_index];
    World* world = &app->world;
    Warehouse* wh = world->sims[(int32_t)side];
    Warehouse* other = world->sims[1 - (int32_t)side];
    float ui = world->live.ui_scale;
    ImU32 accent = SideAccent(side);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImFont* font = ImGui::GetFont();
    float fs = ImGui::GetFontSize();
    draw->PushClipRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), true);

    float cell_px = CellWorld * app->camera.zoom;
    if (cell_px >= 17.0f) {
        float origin_x = rect.x + rect.w * 0.5f - app->camera.x * app->camera.zoom;
        float origin_y = rect.y + rect.h * 0.5f - app->camera.y * app->camera.zoom;
        float label_size = sdl::min(fs, cell_px * 0.42f);
        for (int32_t col = 0; col < wh->cols; col++) {
            char text[24];
            sdl::snprintf(text, sizeof(text), "%d", wh->station_delivered[col]);
            ImVec2 size = font->CalcTextSizeA(label_size, FLT_MAX, 0.0f, text);
            float cx = origin_x + ((float)col + 0.5f) * cell_px;
            float cy = origin_y + ((float)wh->rows + 0.34f) * cell_px;
            draw->AddText(font, label_size, ImVec2(cx - size.x * 0.5f, cy - size.y * 0.5f), IM_COL32(190, 240, 225, 230), text);
        }
        if (cell_px >= 26.0f) {
            float small = sdl::min(fs * 0.8f, cell_px * 0.34f);
            for (int32_t col = 0; col < wh->cols; col++) {
                char text[24];
                sdl::snprintf(text, sizeof(text), "S%d", col + 1);
                ImVec2 size = font->CalcTextSizeA(small, FLT_MAX, 0.0f, text);
                float cx = origin_x + ((float)col + 0.5f) * cell_px;
                float cy = origin_y + ((float)wh->rows + 0.82f) * cell_px;
                draw->AddText(font, small, ImVec2(cx - size.x * 0.5f, cy), IM_COL32(120, 170, 160, 200), text);
            }
        }
    }

    float pad = 12.0f * ui;
    float header_h = HudHeight * ui;
    draw->AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + header_h), IM_COL32(7, 9, 15, 224));
    draw->AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + 4.0f * ui), accent);

    float y = rect.y + 8.0f * ui;
    draw->AddText(font, fs, ImVec2(rect.x + pad, y), accent, wh->controller->Name());

    char clock_text[48];
    sdl::snprintf(clock_text, sizeof(clock_text), "%.1f s", WarehouseClock(wh));
    float big = fs * 2.0f;
    draw->AddText(font, big, ImVec2(rect.x + pad, y + fs * 1.15f), wh->finished ? IM_COL32(120, 255, 150, 255) : IM_COL32(240, 244, 255, 255), clock_text);

    char badge[96];
    ImU32 badge_color = IM_COL32(200, 205, 220, 255);
    if (wh->finished) {
        RaceWinner winner = world->winner;
        bool won = (winner == RaceWinner::Baseline && side == SimSide::Baseline) || (winner == RaceWinner::Flagship && side == SimSide::Flagship);
        if (winner == RaceWinner::Tie) sdl::snprintf(badge, sizeof(badge), "FINISHED - TIE");
        else if (won) sdl::snprintf(badge, sizeof(badge), "FINISHED - WINNER");
        else sdl::snprintf(badge, sizeof(badge), "FINISHED - %.1f s slower", wh->finish_time - other->finish_time);
        badge_color = won ? IM_COL32(120, 255, 150, 255) : IM_COL32(255, 200, 120, 255);
    } else if (other->finished) {
        sdl::snprintf(badge, sizeof(badge), "other fleet finished - %d to go", wh->order_count - wh->delivered);
        badge_color = IM_COL32(255, 150, 120, 255);
    } else {
        int32_t lead = wh->delivered - other->delivered;
        if (lead > 0) { sdl::snprintf(badge, sizeof(badge), "+%d orders ahead", lead); badge_color = IM_COL32(120, 255, 150, 255); }
        else if (lead < 0) { sdl::snprintf(badge, sizeof(badge), "%d orders behind", -lead); badge_color = IM_COL32(255, 150, 120, 255); }
        else sdl::snprintf(badge, sizeof(badge), "level");
    }
    ImVec2 badge_size = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, badge);
    draw->AddText(font, fs, ImVec2(rect.x + rect.w - pad - badge_size.x, y), badge_color, badge);

    float bar_y = y + fs * 3.45f;
    float bar_h = 10.0f * ui;
    float bar_x0 = rect.x + pad, bar_x1 = rect.x + rect.w - pad;
    float fraction = (wh->order_count > 0) ? (float)wh->delivered / (float)wh->order_count : 0.0f;
    draw->AddRectFilled(ImVec2(bar_x0, bar_y), ImVec2(bar_x1, bar_y + bar_h), IM_COL32(40, 46, 62, 255), 3.0f);
    if (fraction > 0.0f) draw->AddRectFilled(ImVec2(bar_x0, bar_y), ImVec2(bar_x0 + (bar_x1 - bar_x0) * fraction, bar_y + bar_h), accent, 3.0f);

    char stats[200];
    if (side == SimSide::Flagship) {
        sdl::snprintf(stats, sizeof(stats), "%d / %d delivered    %.1f orders/min    waiting %.0f%%    sidesteps %d",
            wh->delivered, wh->order_count, OrdersPerMinute(wh), WaitFraction(wh) * 100.0f, wh->stats.sidesteps);
    } else {
        sdl::snprintf(stats, sizeof(stats), "%d / %d delivered    %.1f orders/min    waiting %.0f%%    reroutes %d",
            wh->delivered, wh->order_count, OrdersPerMinute(wh), WaitFraction(wh) * 100.0f, wh->stats.reroutes);
    }
    draw->AddText(font, fs * 0.92f, ImVec2(rect.x + pad, bar_y + bar_h + 4.0f * ui), IM_COL32(190, 198, 218, 255), stats);

    draw->PopClipRect();
}

const char* BotStateName(BotState state) {
    switch (state) {
        case BotState::Idle:      return "idle";
        case BotState::ToPickup:  return "to pick-up";
        case BotState::Picking:   return "picking";
        case BotState::ToDropoff: return "to drop-off";
        case BotState::Dropping:  return "dropping";
        case BotState::Retreat:   return "stepping aside / parking";
    }
    return "?";
}

void InspectRowText(const char* label, const char* baseline_text, const char* flagship_text) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(baseline_text);
    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(flagship_text);
}

void InspectRowInt(const char* label, int32_t baseline_value, int32_t flagship_value) {
    char a[32], b[32];
    sdl::snprintf(a, sizeof(a), "%d", baseline_value);
    sdl::snprintf(b, sizeof(b), "%d", flagship_value);
    InspectRowText(label, a, b);
}

void InspectRowFloat(const char* label, const char* format, float baseline_value, float flagship_value) {
    char a[32], b[32];
    sdl::snprintf(a, sizeof(a), format, baseline_value);
    sdl::snprintf(b, sizeof(b), format, flagship_value);
    InspectRowText(label, a, b);
}

void DrawInspector(App* app) {
    World* world = &app->world;

    for (int32_t id = 0; id < world->scenario->bot_count; id++) {
        bool is_selected = (world->selected_bot_plus1 - 1 == id);
        bool is_pinned = app->pinned_bots[id];

        if (!is_selected && !is_pinned) continue;

        ImGui::SetNextWindowPos(ImVec2(app->sidebar_width + 12.0f + (float)((id * 20) % 200), 140.0f * world->live.ui_scale), ImGuiCond_FirstUseEver);
        bool open = true;
        char title[64];
        sdl::snprintf(title, sizeof(title), "Bot #%d%s###inspector_%d", id, is_pinned ? " (Pinned)" : "", id);

        if (ImGui::Begin(title, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
            bool pin = is_pinned;
            if (ImGui::Checkbox("Pin window", &pin)) {
                app->pinned_bots[id] = pin;
            }

            Warehouse* base = world->sims[(int32_t)SimSide::Baseline];
            Warehouse* flag = world->sims[(int32_t)SimSide::Flagship];
            Bot* a = &base->bots[id];
            Bot* b = &flag->bots[id];

            if (ImGui::BeginTable("inspect", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("");
                ImGui::TableSetupColumn("Baseline");
                ImGui::TableSetupColumn("Flagship");
                ImGui::TableHeadersRow();

                InspectRowText("State", BotStateName(a->state), BotStateName(b->state));
                InspectRowFloat("Speed (cells/s)", "%.2f", a->speed, b->speed);
                InspectRowInt("Carrying", a->cargo_count, b->cargo_count);
                InspectRowInt("Claimed pick-ups", a->pickup_count, b->pickup_count);
                InspectRowInt("Wait ticks now", a->wait_ticks, b->wait_ticks);
                InspectRowInt("Orders delivered", a->orders_done, b->orders_done);
                InspectRowFloat("Time waiting (s)", "%.1f", a->wait_seconds, b->wait_seconds);
                InspectRowInt("Reroutes", a->reroutes, b->reroutes);
                InspectRowFloat("Distance (cells)", "%.0f", a->distance_cells, b->distance_cells);
                ImGui::EndTable();
            }
            HelpText("White line = planned route, red boxes = cells in its local blocked map.");
        }
        ImGui::End();

        if (!open) {
            app->pinned_bots[id] = false;
            if (is_selected) world->selected_bot_plus1 = 0;
        }
    }
}

void DrawWinnerPopup(App* app) {
    World* world = &app->world;
    if (ImGui::BeginPopupModal("Race Winner!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Warehouse* base = world->sims[(int32_t)SimSide::Baseline];
        Warehouse* flag = world->sims[(int32_t)SimSide::Flagship];

        if (world->winner == RaceWinner::Tie) ImGui::TextColored(ImVec4(1, 1, 1, 1), "Dead heat!");
        else if (world->winner == RaceWinner::Baseline) ImGui::TextColored(ImVec4(1.0f, 0.69f, 0.28f, 1), "BASELINE (A* + reservations) wins");
        else ImGui::TextColored(ImVec4(0.35f, 0.88f, 1.0f, 1), "FLAGSHIP (grid boids) wins");

        ImGui::Separator();
        ImGui::Text("Baseline  %3d / %d delivered   %s %.1f s", base->delivered, base->order_count, base->finished ? "finished in" : "running at", WarehouseClock(base));
        ImGui::Text("Flagship  %3d / %d delivered   %s %.1f s", flag->delivered, flag->order_count, flag->finished ? "finished in" : "running at", WarehouseClock(flag));
        if (base->finished && flag->finished) {
            float delta = base->finish_time - flag->finish_time;
            if (delta > 0.0f) ImGui::Text("Flagship was %.1f s faster (%.0f%%).", delta, delta / base->finish_time * 100.0f);
            else if (delta < 0.0f) ImGui::Text("Baseline was %.1f s faster (%.0f%%).", -delta, -delta / flag->finish_time * 100.0f);
        } else {
            ImGui::TextDisabled("Other fleet still running - keep watching for its time.");
        }

        ImGui::Separator();
        ImGui::Text("Leaderboard   Baseline %d   |   Flagship %d   |   Ties %d",
            app->leaderboard_wins[(int32_t)SimSide::Baseline], app->leaderboard_wins[(int32_t)SimSide::Flagship], app->leaderboard_ties);

        if (ImGui::Button("Keep watching")) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("New race (new seed)")) {
            app->pending_restart_cfg.seed = 0;
            AppRebuild(app);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawUi(App* app) {
    ComputeLayout(app);
    RefitIfLayoutChanged(app);

    int32_t window_width, window_height;
    app->window.GetSize(&window_width, &window_height);

    for (int32_t v = 0; v < app->view_count; v++) {
        DrawViewHud(app, v);
    }

    if (app->show_sidebar) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(260.0f * app->world.live.ui_scale, (float)window_height),
            ImVec2((float)window_width * 0.5f, (float)window_height)
        );
        ImGui::SetNextWindowSize(ImVec2(app->sidebar_width, (float)window_height));

        ImGuiWindowFlags sidebar_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##sidebar", nullptr, sidebar_flags)) {
            // Save the width back so it persists when dragging the edge
            app->desired_sidebar_width = ImGui::GetWindowWidth() / app->world.live.ui_scale;

            ImGui::TextColored(ImVec4(0.9f, 0.93f, 1.0f, 1.0f), "HIVE RACE");
            ImGui::SameLine();
            ImGui::TextDisabled(" baseline vs flagship");
            WrappedColoredText(ImVec4(1.0f, 0.69f, 0.28f, 1.0f), app->world.sims[(int32_t)SimSide::Baseline]->controller->Summary());
            WrappedColoredText(ImVec4(0.35f, 0.88f, 1.0f, 1.0f), app->world.sims[(int32_t)SimSide::Flagship]->controller->Summary());
            ImGui::Separator();

            for (int32_t i = 0; i < app->panel_count; i++) {
                UiPanel* panel = app->panels[i];
                ImGuiTreeNodeFlags flags = panel->OpenByDefault() ? ImGuiTreeNodeFlags_DefaultOpen : 0;
                if (ImGui::CollapsingHeader(panel->Title(), flags)) {
                    ImGui::PushID(i);
                    panel->Draw(app);
                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }

    DrawInspector(app);
    DrawWinnerPopup(app);
}
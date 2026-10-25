// main.cpp
#define SDL_MAIN_USE_CALLBACKS 1
#define IMGUI_DEFINE_MATH_OPERATORS
#ifndef APP_NO_FREETYPE
    #define IMGUI_ENABLE_FREETYPE 
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#ifndef APP_NO_FREETYPE
    #include <misc/freetype/imgui_freetype.h>
#endif

// --- unity build: Dear ImGui's own sources, compiled directly into us too ---
#include <imgui.cpp>
#include <imgui_draw.cpp>
#include <imgui_tables.cpp>
#include <imgui_widgets.cpp>
#include <imgui_impl_sdl3.cpp>
#include <imgui_impl_sdlrenderer3.cpp>
#ifndef APP_NO_FREETYPE
    #include <misc/freetype/imgui_freetype.cpp>
#endif

#include "app.hpp"

inline void LoadUiFont(ImGuiIO* io) {
    const char* base_path = SDL_GetBasePath(); 

    static const char* candidate_names[] = {
        "Inter-Regular.ttf", "Inter.ttf", "InterVariable.ttf",
        "fonts/Inter-Regular.ttf", "fonts/InterVariable.ttf",
        "font.ttf"
    };

    for (const char* name : candidate_names) {
        char full_path[1024];

        if (base_path) {
            sdl::snprintf(full_path, sizeof(full_path), "%s%s", base_path, name);
            if (SDL_GetPathInfo(full_path, nullptr)) {
                io->Fonts->AddFontFromFileTTF(full_path, 18.0f);
                sdl::log("loaded UI font: %s", full_path);
                return;
            }

            sdl::snprintf(full_path, sizeof(full_path), "%s../%s", base_path, name);
            if (SDL_GetPathInfo(full_path, nullptr)) {
                io->Fonts->AddFontFromFileTTF(full_path, 18.0f);
                sdl::log("loaded UI font: %s", full_path);
                return;
            }
        }

        if (SDL_GetPathInfo(name, nullptr)) {
            io->Fonts->AddFontFromFileTTF(name, 18.0f);
            sdl::log("loaded UI font: %s", name);
            return;
        }
    }

    sdl::log("no Inter font found next to (or one folder above) the executable - using ImGui's built-in font instead.");
    io->Fonts->AddFontDefault();
}

#ifdef SDL_MAIN_USE_CALLBACKS
    #define GAME_INIT_FN SDL_AppInit
    #define GAME_ITERATE_FN SDL_AppIterate
    #define GAME_EVENT_FN SDL_AppEvent
    #define GAME_QUIT_FN SDL_AppQuit
#else
    #define GAME_INIT_FN game_init
    #define GAME_ITERATE_FN game_iterate
    #define GAME_EVENT_FN game_event
    #define GAME_QUIT_FN game_quit
#endif

SDL_AppResult GAME_INIT_FN(void** appstate, int argc, char* argv[]) {
    Arena* permanent = ArenaAlloc();
    App* app = PushStruct<App>(permanent);
    app->permanent_arena = permanent;
    *appstate = app;

    if (!app->platform.Init()) {
        sdl::log("SDL_Init failed: %s", sdl::get_error());
        return SDL_APP_FAILURE;
    }

    bool window_ok = app->window.Create("Hive Race - A* Baseline vs Grid Boids", DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED);
    if (!window_ok) return SDL_APP_FAILURE;
    if (!app->renderer.Create(&app->window)) return SDL_APP_FAILURE;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; 
    ImGui::StyleColorsDark();
    g_base_ui_style = ImGui::GetStyle(); 
    LoadUiFont(&io);
    ImGui_ImplSDL3_InitForSDLRenderer(app->window.handle, app->renderer.handle);
    ImGui_ImplSDLRenderer3_Init(app->renderer.handle);

    app->world.live = MakeDefaultLiveConfig();
    app->pending_restart_cfg = MakeDefaultRestartConfig();
    if (argc > 1) app->pending_restart_cfg.seed = sdl::str_to_u64(argv[1], nullptr, 10);
    app->ui_scale_target = 1.0f;
    app->world.live.ui_scale = 1.0f;
    app->view_mode = ViewMode::Split;
    app->rebuild_on_grid_edit = true;

    app->render_arena = ArenaAlloc();
    app->vertices.Init(app->render_arena, VertexCapacity);
    app->indices.Init(app->render_arena, IndexCapacity);

    app->refresh_rate_hz = app->window.RefreshRate();
    app->render_period_ns = (uint64_t)(1.0e9 / (double)app->refresh_rate_hz);

    InitUiPanels(app);
    AppRebuild(app); 
    app->paused = true; 

    sdl::log("seed %" SDL_PRIu64 " (pass it as argv[1] to recreate this exact race)", app->world.restart_cfg.seed);

    return SDL_APP_CONTINUE;
}

SDL_AppResult GAME_EVENT_FN(void* appstate, SDL_Event* event) {
    App* app = (App*)appstate;
    ImGui_ImplSDL3_ProcessEvent(event);
    ImGuiIO& io = ImGui::GetIO();

    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;

    if (event->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode key = event->key.key;
        bool ctrl_held = (event->key.mod & SDL_KMOD_CTRL) != 0;

        if (ctrl_held && (key == SDLK_EQUALS || key == SDLK_KP_PLUS)) {
            app->ui_scale_target = sdl::min(app->ui_scale_target + UI_SCALE_STEP, UI_SCALE_MAX);
            return SDL_APP_CONTINUE;
        }
        if (ctrl_held && (key == SDLK_MINUS || key == SDLK_KP_MINUS)) {
            app->ui_scale_target = sdl::max(app->ui_scale_target - UI_SCALE_STEP, UI_SCALE_MIN);
            return SDL_APP_CONTINUE;
        }

        if (!io.WantTextInput) { // Changed from WantCaptureKeyboard so hotkeys work while inspecting
            LiveConfig* live = &app->world.live;
            ViewMode previous_mode = app->view_mode;
            switch (key) {
                case SDLK_P:
                case SDLK_SPACE: app->paused = !app->paused; break;
                case SDLK_N: app->paused = true; app->step_once = true; break;
                case SDLK_R: AppRebuild(app); break;
                case SDLK_F: FitCamera(app); break;
                case SDLK_H: app->show_sidebar = !app->show_sidebar; break;
                case SDLK_D: live->debug_overlay = !live->debug_overlay; break;
                case SDLK_T: live->show_trails = !live->show_trails; break;
                case SDLK_O: live->show_paths = !live->show_paths; break;
                case SDLK_1: app->view_mode = ViewMode::Split; break;
                case SDLK_2: app->view_mode = ViewMode::BaselineOnly; break;
                case SDLK_3: app->view_mode = ViewMode::FlagshipOnly; break;
                case SDLK_ESCAPE: app->world.selected_bot_plus1 = 0; break;
                default: break;
            }
            if (app->view_mode != previous_mode) FitCamera(app);
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (io.WantCaptureMouse) return SDL_APP_CONTINUE; 

        if (event->button.button == SDL_BUTTON_RIGHT) {
            app->dragging = true;
            app->camera_auto_fit = false;
            app->drag_x = event->button.x;
            app->drag_y = event->button.y;
        } else if (event->button.button == SDL_BUTTON_LEFT) {
            int32_t view = ViewIndexAt(app, event->button.x, event->button.y);
            if (view >= 0) {
                ViewRect rect = app->view_rect[view];
                float origin_x = rect.x + rect.w * 0.5f - app->camera.x * app->camera.zoom;
                float origin_y = rect.y + rect.h * 0.5f - app->camera.y * app->camera.zoom;
                Vec2 world_point = app->camera.LogicalToWorld(event->button.x, event->button.y, origin_x, origin_y);

                Warehouse* wh = app->world.sims[(int32_t)app->view_side[view]];
                Bot* bot = FindBotNearWorldPoint(wh, world_point, CellWorld * 0.7f);
                app->world.selected_bot_plus1 = bot ? bot->id + 1 : 0;
            }
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_RIGHT) app->dragging = false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (app->dragging) {
            app->camera.x -= (event->motion.x - app->drag_x) / app->camera.zoom;
            app->camera.y -= (event->motion.y - app->drag_y) / app->camera.zoom;
            app->drag_x = event->motion.x;
            app->drag_y = event->motion.y;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        if (io.WantCaptureMouse) return SDL_APP_CONTINUE;

        float mouse_x, mouse_y;
        sdl::get_mouse_state(&mouse_x, &mouse_y);
        int32_t view = ViewIndexAt(app, mouse_x, mouse_y);
        if (view < 0) return SDL_APP_CONTINUE;

        ViewRect rect = app->view_rect[view];
        float from_center_x = mouse_x - (rect.x + rect.w * 0.5f);
        float from_center_y = mouse_y - (rect.y + rect.h * 0.5f);
        float world_x = from_center_x / app->camera.zoom_target + app->camera.x;
        float world_y = from_center_y / app->camera.zoom_target + app->camera.y;

        app->camera_auto_fit = false;
        float zoom_step = ZOOM_STEP_IN;
        if (event->wheel.y <= 0) zoom_step = ZOOM_STEP_OUT;
        app->camera.zoom_target = sdl::clamp(app->camera.zoom_target * zoom_step, ZOOM_MIN, ZOOM_MAX);

        app->camera.x = world_x - from_center_x / app->camera.zoom_target;
        app->camera.y = world_y - from_center_y / app->camera.zoom_target;
    }

    if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
        app->refresh_rate_hz = app->window.RefreshRate();
        app->render_period_ns = (uint64_t)(1.0e9 / (double)app->refresh_rate_hz);
        UpdateMainCallbackRate(app);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult GAME_ITERATE_FN(void* appstate) {
    App* app = (App*)appstate;
    World* world = &app->world;

    uint64_t now_ns = sdl::now_ns();
    uint64_t frame_ns = now_ns - app->last_iteration_ns;
    app->last_iteration_ns = now_ns;

    if (!app->paused) app->sim_accumulator_ns += (uint64_t)((double)frame_ns * (double)world->live.sim_speed);
    if (app->sim_accumulator_ns > MAX_BANKED_NS) app->sim_accumulator_ns = MAX_BANKED_NS;

    float step_delta_seconds = (float)app->tick_period_ns / 1.0e9f;
    int32_t steps_this_call = 0;
    while (app->sim_accumulator_ns >= app->tick_period_ns && steps_this_call < MAX_STEPS_PER_ITERATE) {
        WorldTick(world, step_delta_seconds);
        app->sim_accumulator_ns -= app->tick_period_ns;
        app->stats_ticks++;
        steps_this_call++;
    }
    if (app->step_once) {
        WorldTick(world, step_delta_seconds);
        app->stats_ticks++;
        app->step_once = false;
    }

    float alpha = 1.0f;
    if (!app->paused && world->live.sim_speed <= 1.5f) {
        alpha = sdl::clamp((float)app->sim_accumulator_ns / (float)app->tick_period_ns, 0.0f, 1.0f);
    }

    app->render_accumulator_ns += frame_ns;
    if (app->render_accumulator_ns >= app->render_period_ns) {
        app->render_accumulator_ns -= app->render_period_ns;
        if (app->render_accumulator_ns >= app->render_period_ns) app->render_accumulator_ns = 0;

        float render_delta_seconds = (float)(now_ns - app->last_render_ns) * 1.0e-9f;
        app->last_render_ns = now_ns;

        UpdateUiScale(app, render_delta_seconds);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        CheckRaceWinner(app); 

        DrawUi(app);
        RenderAll(app, alpha, render_delta_seconds); 
        app->renderer.Present();
        app->stats_frames++;
    }

    uint64_t stats_elapsed_ns = now_ns - app->stats_window_start_ns;
    if (stats_elapsed_ns >= StatsWindowNs) {
        float elapsed_seconds = (float)stats_elapsed_ns * 1.0e-9f;
        app->measured_tick_rate = (float)app->stats_ticks / elapsed_seconds;
        app->measured_fps = (float)app->stats_frames / elapsed_seconds;
        app->stats_ticks = 0;
        app->stats_frames = 0;
        app->stats_window_start_ns = now_ns;
    }

    return SDL_APP_CONTINUE;
}

void GAME_QUIT_FN(void* appstate, SDL_AppResult result) {
    (void)result;
    App* app = (App*)appstate;
    if (!app) return;

    if (app->world.arena) app->world.arena->Release();
    if (app->render_arena) app->render_arena->Release();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    for (int32_t v = 0; v < SimSideCount; v++) app->view_target[v].Destroy();
    app->renderer.Destroy();
    app->window.Destroy();
    app->platform.Quit();

    app->permanent_arena->Release();
}

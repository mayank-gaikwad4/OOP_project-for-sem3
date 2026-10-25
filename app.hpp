// app.hpp
#pragma once
#include "world.hpp"
#include "renderer.hpp"
#include "sdl_wrapper.hpp"
#include <imgui.h>

constexpr float UI_SCALE_GLIDE_RATE = 18.0f;//like the zooming rate(using lerpexp)
constexpr float ZOOM_STEP_IN = 1.06f;
constexpr float ZOOM_STEP_OUT = 0.94f;
constexpr float ZOOM_MIN = 0.04f;
constexpr float ZOOM_MAX = 15.0f;
constexpr float UI_SCALE_STEP = 0.15f;
constexpr float UI_SCALE_MIN = 0.5f;
constexpr float UI_SCALE_MAX = 3.0f;
constexpr int32_t MAX_STEPS_PER_ITERATE = 256;
constexpr uint64_t MAX_BANKED_NS = UINT64_C(2'000'000'000);
//applys the usuasl ull or ul or whatever is platform appropriate
//in stdint.h ,, and is a macro
constexpr int32_t DEFAULT_WINDOW_WIDTH = 1920;
constexpr int32_t DEFAULT_WINDOW_HEIGHT = 1080;

constexpr int32_t SupersampleFactor = 2;
//supersampling antialiasing factor or SSAA
//so basically renders up everything to like twice the resolution
//and then scales it down to look good
//expensive and simple and just works

constexpr int32_t MinSimStepsPerSecond = 5;//so like a min tick rate
constexpr int32_t MaxSimStepsPerSecond = 480;//max tick rate
constexpr uint64_t StatsWindowNs = UINT64_C(500'000'000);
constexpr int32_t VertexCapacity = MaxCells * 16 + MaxBots * 160 + 16'384;
//for the vertex and index buffers
constexpr int32_t IndexCapacity = MaxCells * 24 + MaxBots * 260 + 32'768;
constexpr int32_t MaxPanels = 8;//count of the drop down expanding menus in the side bar
constexpr float ViewGap = 6.0f;      
constexpr float HudHeight = 128.0f;  

constexpr ImU32 AccentBaseline = IM_COL32(255, 176, 72, 255);
constexpr ImU32 AccentFlagship = IM_COL32(88, 224, 255, 255);

class ViewRect {
public:
    float x, y, w, h; 
};

class UiPanel {
public:
    virtual ~UiPanel() {}
    virtual const char* Title() const = 0;
    virtual bool OpenByDefault() const = 0;
    virtual void Draw(App* app) = 0;
};

class App {
public:
    sdl::Platform platform;
    sdl::Window window;
    sdl::Renderer renderer;
    sdl::RenderTarget view_target[SimSideCount]; 

    Arena* permanent_arena; 

    World world;
    Camera camera;

    Arena* render_arena; 
    VertexBuffer vertices;
    IndexBuffer indices;

    RestartConfig pending_restart_cfg;

    ViewMode view_mode;
    int32_t view_count;
    SimSide view_side[SimSideCount];
    ViewRect view_rect[SimSideCount];
    float sidebar_width;

    UiPanel* panels[MaxPanels];

    int32_t panel_count;

    bool camera_auto_fit;
    int32_t last_fit_width, last_fit_height;
    float last_fit_sidebar;
    ViewMode last_fit_mode;

    bool paused;
    bool show_sidebar = true;
    float desired_sidebar_width = 400.0f;
    bool pinned_bots[MaxBots] = {}; // Tracks which bots have their inspector pinned
    bool step_once;            
    bool rebuild_on_grid_edit; 
    bool dragging;
    float drag_x, drag_y; 
    float ui_scale_target; 

    bool race_winner_announced;
    int32_t leaderboard_wins[SimSideCount];
    int32_t leaderboard_ties;

    float refresh_rate_hz;
    uint64_t last_iteration_ns;
    uint64_t last_render_ns;
    uint64_t sim_accumulator_ns;
    uint64_t render_accumulator_ns;
    uint64_t render_period_ns;
    uint64_t tick_period_ns;
    uint64_t stats_window_start_ns;
    int32_t stats_ticks;
    int32_t stats_frames;
    float measured_tick_rate;
    float measured_fps;

    void ResetLeaderboard() {
        for (int32_t i = 0; i < SimSideCount; i++) leaderboard_wins[i] = 0;
        leaderboard_ties = 0;
    }
};

extern ImGuiStyle g_base_ui_style;

void UpdateMainCallbackRate(App* app);
void ApplySimRate(App* app);
void ComputeLayout(App* app);
int32_t ViewIndexAt(const App* app, float x, float y);
void FitCamera(App* app);
void RefitIfLayoutChanged(App* app);
void AppRebuild(App* app);
Bot* FindBotNearWorldPoint(Warehouse* wh, Vec2 world_point, float max_distance);
void UpdateUiScale(App* app, float delta_seconds);
void CheckRaceWinner(App* app);

float WarehouseClock(const Warehouse* wh);
float OrdersPerMinute(const Warehouse* wh);
float AverageLatency(const Warehouse* wh);
float WaitFraction(const Warehouse* wh);
void HelpText(const char* text);
void WrappedColoredText(ImVec4 color, const char* text);
void ResultRow(const char* label, const char* format, float baseline_value, float flagship_value, bool lower_is_better);

class RunPanel : public UiPanel {
public:
    const char* Title() const override { return "Run & View"; }
    bool OpenByDefault() const override { return true; }
    void Draw(App* app) override;
};

class ResultsPanel : public UiPanel {
public:
    const char* Title() const override { return "Race results"; }
    bool OpenByDefault() const override { return true; }
    void Draw(App* app) override;
};

class ScenarioPanel : public UiPanel {
public:
    const char* Title() const override { return "Scenario  (applies on restart)"; }
    bool OpenByDefault() const override { return true; }
    void Draw(App* app) override;
};

class KinematicsPanel : public UiPanel {
public:
    const char* Title() const override { return "Kinematics & timers"; }
    bool OpenByDefault() const override { return false; }
    void Draw(App* app) override;
};

class TaskingPanel : public UiPanel {
public:
    const char* Title() const override { return "Orders & baseline rules"; }
    bool OpenByDefault() const override { return false; }
    void Draw(App* app) override;
};

class FlagshipPanel : public UiPanel {
public:
    const char* Title() const override { return "Flagship: grid boid rules"; }
    bool OpenByDefault() const override { return false; }
    void Draw(App* app) override;
};

class DisplayPanel : public UiPanel {
public:
    const char* Title() const override { return "Display & performance"; }
    bool OpenByDefault() const override { return false; }
    void Draw(App* app) override;
};

void InitUiPanels(App* app);
void DrawViewHud(App* app, int32_t view_index);
const char* BotStateName(BotState state);
void InspectRowText(const char* label, const char* baseline_text, const char* flagship_text);
void InspectRowInt(const char* label, int32_t baseline_value, int32_t flagship_value);
void InspectRowFloat(const char* label, const char* format, float baseline_value, float flagship_value);
void DrawInspector(App* app);
void DrawWinnerPopup(App* app);
void DrawUi(App* app);

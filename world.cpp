// world.cpp
#include "world.hpp"
#include "simulation.hpp"

constexpr int32_t MAX_BOT_PLACEMENT_ATTEMPTS = 10000;
constexpr int32_t MAX_CRATE_PLACEMENT_ATTEMPTS = 12;
constexpr float POISSON_MAX_U = 0.9999f;

LiveConfig MakeDefaultLiveConfig() {
    LiveConfig c = {};
    c.max_speed = 4.0f;
    c.accel = 7.0f;
    c.decel = 10.0f;
    c.turn_speed = 1.2f;
    c.clear_hold_seconds = 0.05f;
    c.body_clear_progress = 0.9f;
    c.reserve_margin = 0.25f;

    c.pickup_seconds = 1.0f;
    c.dropoff_seconds = 1.5f;

    c.yield_tick_seconds = 0.25f;
    c.wait_tick_limit = 3;
    c.blocked_memory_seconds = 5.0f;

    c.assign_policy = AssignPolicy::Nearest;
    c.bot_capacity = 2;
    c.pickup_on_the_way = true;
    c.pickup_detour = 1;

    c.use_congestion = true;
    c.congestion_weight = 1.2f;
    c.crowd_radius = 2;        
    // 2 means 5x5, 1 means 3x3, 3 means 7x7
    c.auto_crowd_trigger = true;
    c.crowd_trigger = 11;
    c.use_lanes = true;
    c.lane_weight = 0.5f;
    c.lane_memory_seconds = 6.0f;
    c.use_platoons = true;
    c.platoon_follow_progress = 0.75f;
    c.use_sidestep = true;
    c.turn_penalty = 0.6f;
    c.retire_idle_bots = true;

    c.sim_steps_per_second = 60;
    c.sim_speed = 1.0f;
    c.debug_overlay = true;
    c.show_trails = true;
    c.show_paths = false;
    c.ui_scale = 1.0f;
    return c;
}

RestartConfig MakeDefaultRestartConfig() {
    RestartConfig c = {};
    c.cols = 24;
    c.rows = 16;
    c.bot_count = 24;
    c.orders_to_win = 100;
    c.initial_backlog = 30;
    c.order_rate = 2.0f;
    c.seed = 0;
    return c;
}

void ClampRestartConfig(RestartConfig* c) {
    c->cols = sdl::clamp(c->cols, 4, MaxGridCols);
    c->rows = sdl::clamp(c->rows, 4, MaxGridRows);
    int32_t free_cells = c->cols * (c->rows - 1);
    c->bot_count = sdl::clamp(c->bot_count, 1, sdl::min(MaxBots, free_cells));
    c->orders_to_win = sdl::clamp(c->orders_to_win, 1, MaxOrders);
    c->initial_backlog = sdl::clamp(c->initial_backlog, 0, c->orders_to_win);
    c->order_rate = sdl::clamp(c->order_rate, 0.1f, 50.0f);
}

Scenario* BuildScenario(Arena* arena, const RestartConfig* cfg) {
    Scenario* s = PushStruct<Scenario>(arena);
    s->seed = cfg->seed;
    s->cols = cfg->cols;
    s->rows = cfg->rows;

    uint64_t rng = cfg->seed + SDL_UINT64_C(0x9E3779B97F4A7C15);
    rng = (rng ^ (rng >> 30)) * SDL_UINT64_C(0xBF58476D1CE4E5B9);
    rng = (rng ^ (rng >> 27)) * SDL_UINT64_C(0x94D049BB133111EB);
    rng ^= (rng >> 31);

    uint8_t used[MaxCells] = {};
    s->bot_count = cfg->bot_count;
    for (int32_t b = 0; b < cfg->bot_count; b++) {
        int32_t cell = 0;
        for (int32_t attempt = 0; attempt < MAX_BOT_PLACEMENT_ATTEMPTS; attempt++) {
            cell = sdl::rand_below(&rng, cfg->cols * (cfg->rows - 1));
            if (!used[cell]) 
                break;
        }
        used[cell] = 1;
        s->bot_start_cell[b] = cell;
        s->bot_color[b] = (uint8_t)sdl::rand_below(&rng, colors::DistinctColorCount);
    }

    uint8_t crate_used[MaxCells] = {};
    s->order_count = cfg->orders_to_win;
    float release = 0.0f;
    for (int32_t o = 0; o < cfg->orders_to_win; o++) {
        int32_t cell = 0;
        for (int32_t attempt = 0; attempt < MAX_CRATE_PLACEMENT_ATTEMPTS; attempt++) {
            cell = sdl::rand_below(&rng, cfg->cols * (cfg->rows - 1));
            if (!crate_used[cell]) 
                break;
        }
        crate_used[cell] = 1;

        OrderSpec* spec = &s->orders[o];
        spec->pick_cell = cell;
        spec->station_col = sdl::rand_below(&rng, cfg->cols);
        spec->color_index = (uint8_t)sdl::rand_below(&rng, colors::DistinctColorCount);

        if (o >= cfg->initial_backlog) {
            float u = sdl::rand01(&rng);
            if (u > POISSON_MAX_U) 
                u = POISSON_MAX_U;
            release += -sdl::logf(1.0f - u) / cfg->order_rate;
        }
        spec->release_time = release;
    }
    return s;
}

void RebuildWorld(World* world) {
    if (world->arena) world->arena->Release();
    world->arena = nullptr;

    ClampRestartConfig(&world->restart_cfg);
    if (world->restart_cfg.seed == 0) {
        uint64_t seed = sdl::now_ns();
        seed ^= seed >> 17;
        if (seed == 0) seed = 1;
        world->restart_cfg.seed = seed;
    }

    world->arena = ArenaAlloc();
    world->scenario = BuildScenario(world->arena, &world->restart_cfg);
    for (int32_t s = 0; s < SimSideCount; s++) {
        world->sims[s] = BuildWarehouse(world->arena, world->scenario, &world->live, (SimSide)s);
    }
    world->winner = RaceWinner::None;
    world->selected_bot_plus1 = 0;
}

void UpdateWinner(World* world) {
    if (world->winner != RaceWinner::None) return;
    Warehouse* baseline = world->sims[(int32_t)SimSide::Baseline];
    Warehouse* flagship = world->sims[(int32_t)SimSide::Flagship];

    if (baseline->finished && flagship->finished) {
        if (baseline->finish_time < flagship->finish_time) world->winner = RaceWinner::Baseline;
        else if (flagship->finish_time < baseline->finish_time) world->winner = RaceWinner::Flagship;
        else world->winner = RaceWinner::Tie;
    } else if (baseline->finished) {
        world->winner = RaceWinner::Baseline;
    } else if (flagship->finished) {
        world->winner = RaceWinner::Flagship;
    }
}

void WorldTick(World* world, float dt) {
    for (int32_t s = 0; s < SimSideCount; s++) WarehouseTick(world->sims[s], dt);
    UpdateWinner(world);
}

bool WorldAllFinished(const World* world) {
    return world->sims[0]->finished && world->sims[1]->finished;
 }

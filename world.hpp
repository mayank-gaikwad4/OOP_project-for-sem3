// world.hpp
#pragma once
#include "base_arena.hpp"
#include "colors.hpp"

constexpr int32_t MaxGridCols = 64;
constexpr int32_t MaxGridRows = 40;
constexpr int32_t MaxCells = MaxGridCols * MaxGridRows;
constexpr int32_t MaxBots = 128;
constexpr int32_t MaxOrders = 1000;
constexpr int32_t MaxCargo = 4;           
constexpr int32_t MaxPathLength = 320;
constexpr int32_t MaxBlockedMemory = 8;   
constexpr int32_t TrailLength = 14;
constexpr float CellWorld = 40.0f;      

class Warehouse;
class Bot;
class Controller;
class GridAStar;

enum class Dir4 : uint8_t { East = 0, South, West, North };
enum class SimSide : uint8_t { Baseline = 0, Flagship };
constexpr int32_t SimSideCount = 2;
enum class ViewMode : uint8_t { Split = 0, BaselineOnly, FlagshipOnly };
enum class AssignPolicy : uint8_t { Nearest = 0, Oldest };
enum class RaceWinner : uint8_t { None = 0, Baseline, Flagship, Tie };
enum class BotState : uint8_t { Idle = 0, ToPickup, Picking, ToDropoff, Dropping, Retreat };
enum class OrderState : uint8_t { NotReleased = 0, Available, Claimed, Carried, Delivered };

constexpr int32_t DirDX[4] = { 1, 0, -1, 0 };
constexpr int32_t DirDY[4] = { 0, 1, 0, -1 };

inline int32_t DirIndex(Dir4 dir) { return (int32_t)dir; }
inline Dir4 DirFromIndex(int32_t index) { return (Dir4)(index & 3); }
inline Dir4 DirOpposite(Dir4 dir) { return DirFromIndex(DirIndex(dir) + 2); }
inline bool DirIsHorizontal(Dir4 dir) { return (DirIndex(dir) & 1) == 0; }
inline float DirTurns(Dir4 dir) { return (float)DirIndex(dir) * TurnQuarter; }

inline int32_t CellCol(int32_t cell, int32_t cols) { return cell % cols; }
inline int32_t CellRow(int32_t cell, int32_t cols) { return cell / cols; }
inline int32_t CellIndex(int32_t col, int32_t row, int32_t cols) { return row * cols + col; }
inline int32_t Manhattan(int32_t a, int32_t b, int32_t cols) {
    return sdl::abs(CellCol(a, cols) - CellCol(b, cols)) + sdl::abs(CellRow(a, cols) - CellRow(b, cols));
}
inline Vec2 CellCenter(int32_t cell, int32_t cols) { return Vec2((float)CellCol(cell, cols), (float)CellRow(cell, cols)); }

inline int32_t NeighborCell(int32_t cell, Dir4 dir, int32_t cols, int32_t rows) {
    int32_t col = CellCol(cell, cols) + DirDX[DirIndex(dir)];
    int32_t row = CellRow(cell, cols) + DirDY[DirIndex(dir)];
    if (col < 0 || row < 0 || col >= cols || row >= rows)
        return -1;
    return CellIndex(col, row, cols);
}

inline Dir4 DirBetween(int32_t from, int32_t to, int32_t cols) {
    int32_t dx = CellCol(to, cols) - CellCol(from, cols);
    int32_t dy = CellRow(to, cols) - CellRow(from, cols);
    if (dx > 0) 
        return Dir4::East;
    if (dx < 0) 
        return Dir4::West;
    if (dy > 0) 
        return Dir4::South;
    return Dir4::North;
}

inline int32_t StationCell(int32_t station_col, int32_t cols, int32_t rows) { return CellIndex(station_col, rows - 1, cols); }
inline bool IsStationCell(int32_t cell, int32_t cols, int32_t rows) { return CellRow(cell, cols) == rows - 1; }

class LiveConfig {
public:
    float max_speed;            
    float accel;                
    float decel;                
    float turn_speed;           
    float clear_hold_seconds;   
    float body_clear_progress;  
    float reserve_margin;       

    float pickup_seconds;
    float dropoff_seconds;

    float yield_tick_seconds;   
    int32_t wait_tick_limit;      
    float blocked_memory_seconds; 

    AssignPolicy assign_policy;
    int32_t bot_capacity;         
    bool pickup_on_the_way;
    int32_t pickup_detour;        

    bool use_congestion;   
    int32_t crowd_radius;
    bool auto_crowd_trigger;
    float congestion_weight;
    int32_t crowd_trigger;        
    bool use_lanes;           
    float lane_weight;
    float lane_memory_seconds;
    bool use_platoons;        
    float platoon_follow_progress;
    bool use_sidestep;
    float turn_penalty;
    bool retire_idle_bots;

    int32_t sim_steps_per_second;
    float sim_speed;            
    bool debug_overlay;
    bool show_trails;
    bool show_paths;
    float ui_scale;
};

class RestartConfig {
public:
    int32_t cols;
    int32_t rows;
    int32_t bot_count;
    int32_t orders_to_win;      
    int32_t initial_backlog;    
    float order_rate;         
    uint64_t seed;               
};

class OrderSpec {
public:
    int32_t pick_cell;       
    int32_t station_col;     
    float release_time;    
    uint8_t color_index;      
};

class Scenario {
public:
    uint64_t seed;
    int32_t cols, rows;
    int32_t bot_count;
    int32_t bot_start_cell[MaxBots];
    uint8_t bot_color[MaxBots];
    int32_t order_count;
    OrderSpec orders[MaxOrders];   
};

class Bot {
public:
    int32_t id;                       
    BotState state;
    bool retired;
    int32_t cell;                     
    bool moving;                  
    int32_t next_cell;                
    float progress;                 
    float speed;                    
    Dir4 dir;                     
    float heading_turns;            
    Vec2 pos, prev_pos;           

    bool released_from;           
    bool ahead_reserved;          

    int32_t path[MaxPathLength];      
    int32_t path_length;
    int32_t path_index;
    bool has_target;
    int32_t target_cell;
    uint32_t tiebreak;                 

    int32_t wait_ticks;
    float wait_accum;               
    bool giving_way;              
    float hold_until;               
    int32_t best_distance_plus1;      
    int32_t stuck_count;              
    int32_t blocked_cell[MaxBlockedMemory];
    float blocked_until[MaxBlockedMemory]; 
    int32_t blocked_next_slot;

    int32_t pickups[MaxCargo];        
    int32_t pickup_count;
    int32_t cargo[MaxCargo];
    int32_t cargo_count;
    float action_timer;

    float wait_seconds;
    float distance_cells;
    int32_t orders_done;
    int32_t reroutes;

    Vec2 trail[TrailLength];
    int32_t trail_head;
    int32_t trail_count;
    float trail_timer;
};

class OrderRuntime {
public:
    OrderState state;
    int32_t carrier_plus1;            
    float delivered_time;
};

class WarehouseStats {
public:
    int32_t reroutes;                 
    int32_t yield_ticks;              
    int32_t sidesteps;                
    int32_t on_the_way_pickups;
    int32_t unstick_moves;            
    float wait_seconds;             
    float distance_cells;           
    float latency_sum;              
};

class Controller {
public:
    virtual ~Controller() {}

    virtual const char* Name() const = 0;
    virtual const char* Summary() const = 0;

    virtual void BeginTick(Warehouse* wh, float dt) = 0;

    virtual float StepCost(Warehouse* wh, Bot* bot, int32_t from, int32_t to, int32_t prev_dir, int32_t goal) = 0;

    virtual bool CanEnter(Warehouse* wh, Bot* bot, int32_t cell, int32_t from_cell) = 0;
    virtual void TakeCell(Warehouse* wh, Bot* bot, int32_t cell) = 0;

    virtual void RefineAhead(Warehouse* wh, Bot* bot) = 0;

    virtual void OnBlocked(Warehouse* wh, Bot* bot, int32_t blocked_cell) = 0;

    virtual void OnSegmentStart(Warehouse* wh, Bot* bot, int32_t from, int32_t to) = 0;

    virtual float HeatAt(Warehouse* wh, int32_t cell) = 0;
    virtual float LaneBias(Warehouse* wh, bool is_row, int32_t index) = 0;
};

class Warehouse {
public:
    Arena* arena;                 
    Controller* controller;
    GridAStar* astar;
    const Scenario* scenario;
    const LiveConfig* live;
    SimSide side;

    int32_t cols, rows;
    float time;
    uint32_t tick_index;
    bool finished;
    float finish_time;
    int32_t delivered;

    int32_t bot_count;
    Bot* bots;
    int32_t order_count;
    OrderRuntime* orders;
    int32_t next_release;             

    uint8_t* cell_owner;               
    float* cell_hold_until;         
    int32_t* cell_order;              
    int32_t station_delivered[MaxGridCols];

    WarehouseStats stats;
};

class World {
public:
    Arena* arena;                 

    LiveConfig live;              
    RestartConfig restart_cfg;

    Scenario* scenario;
    Warehouse* sims[SimSideCount];
    RaceWinner winner;
    int32_t selected_bot_plus1;       
};

LiveConfig MakeDefaultLiveConfig();
RestartConfig MakeDefaultRestartConfig();
void ClampRestartConfig(RestartConfig* c);
Scenario* BuildScenario(Arena* arena, const RestartConfig* cfg);
void RebuildWorld(World* world);
void UpdateWinner(World* world);
void WorldTick(World* world, float dt);
bool WorldAllFinished(const World* world);

bool PlanPath(Warehouse* wh, Bot* bot, int32_t goal);
void MarkBlocked(Warehouse* wh, Bot* bot, int32_t cell);
bool IsBlockedInMemory(const Warehouse* wh, const Bot* bot, int32_t cell);
bool CellIsFree(const Warehouse* wh, const Bot* bot, int32_t cell);

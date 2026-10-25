// simulation.hpp
#pragma once
#include "world.hpp"

constexpr int32_t HeapCapacity = MaxCells * 4 + 16;
constexpr float CongestionScale = 0.0324f;
constexpr int32_t StuckRerouteLimit = 2;
constexpr float GiveWaySeconds = 2.0f; 


constexpr float ASTAR_F_TOLERANCE = 0.00001f;
constexpr float PENALTY_STATIONARY_BOT = 3.0f;
constexpr float PENALTY_OCCUPIED_CELL = 8.0f;
constexpr float PENALTY_UNPRODUCTIVE_LATERAL = 2.0f;
constexpr float SCORE_IMPROVEMENT_THRESHOLD = 0.5f;
constexpr float HEADING_DECAY_RATE = 22.0f;
constexpr float TRAIL_EMIT_INTERVAL = 0.07f;

enum class MoveResult : uint8_t { Moving = 0, Waiting, Arrived };

class GridAStar {
public:
    float g_cost[MaxCells];
    int32_t parent[MaxCells];
    uint32_t seen_epoch[MaxCells];
    uint32_t closed_epoch[MaxCells];
    uint32_t epoch;

    int32_t heap_cell[HeapCapacity];
    float heap_f[HeapCapacity];
    int32_t heap_h[HeapCapacity];
    uint32_t heap_seq[HeapCapacity];
    int32_t heap_count;
    uint32_t sequence;

    bool HeapLess(int32_t a, int32_t b) const {
        if (heap_f[a] < heap_f[b] - ASTAR_F_TOLERANCE) return true;
        if (heap_f[a] > heap_f[b] + ASTAR_F_TOLERANCE) return false;
        if (heap_h[a] != heap_h[b]) return heap_h[a] < heap_h[b];
        return heap_seq[a] < heap_seq[b];
    }

    void HeapSwap(int32_t a, int32_t b) {
        int32_t tc = heap_cell[a]; heap_cell[a] = heap_cell[b]; heap_cell[b] = tc;
        float tf = heap_f[a];    heap_f[a] = heap_f[b];       heap_f[b] = tf;
        int32_t th = heap_h[a];    heap_h[a] = heap_h[b];       heap_h[b] = th;
        uint32_t ts = heap_seq[a];  heap_seq[a] = heap_seq[b];   heap_seq[b] = ts;
    }

    void HeapPush(int32_t cell, float f, int32_t h) {
        if (heap_count >= HeapCapacity) return;
        int32_t i = heap_count++;
        heap_cell[i] = cell;
        heap_f[i] = f;
        heap_h[i] = h;
        heap_seq[i] = sequence++;
        while (i > 0) {
            int32_t up = (i - 1) / 2;
            if (!HeapLess(i, up)) break;
            HeapSwap(i, up);
            i = up;
        }
    }

    int32_t HeapPop() {
        int32_t result = heap_cell[0];
        heap_count--;
        if (heap_count > 0) {
            heap_cell[0] = heap_cell[heap_count];
            heap_f[0] = heap_f[heap_count];
            heap_h[0] = heap_h[heap_count];
            heap_seq[0] = heap_seq[heap_count];
            int32_t i = 0;
            for (;;) {
                int32_t l = i * 2 + 1;
                int32_t r = l + 1;
                int32_t smallest = i;
                if (l < heap_count && HeapLess(l, smallest)) smallest = l;
                if (r < heap_count && HeapLess(r, smallest)) smallest = r;
                if (smallest == i) break;
                HeapSwap(i, smallest);
                i = smallest;
            }
        }
        return result;
    }

    bool Find(Warehouse* wh, Bot* bot, int32_t start, int32_t goal, int32_t* out_path, int32_t max_length, int32_t* out_length, bool respect_memory);
};

class BaselineController : public Controller {
public:
    const char* Name() const override { return "BASELINE  A* + reservations"; }
    const char* Summary() const override { return "Shortest path, reserve next cell, yield, reroute after 3 wait ticks."; }

    void BeginTick(Warehouse*, float) override {}

    float StepCost(Warehouse*, Bot*, int32_t, int32_t, int32_t, int32_t) override { return 1.0f; }
    bool CanEnter(Warehouse* wh, Bot* bot, int32_t cell, int32_t) override;
    void TakeCell(Warehouse* wh, Bot* bot, int32_t cell) override;

    void RefineAhead(Warehouse*, Bot*) override {}

    void OnBlocked(Warehouse* wh, Bot* bot, int32_t blocked_cell) override;

    void OnSegmentStart(Warehouse*, Bot*, int32_t, int32_t) override {}

    float HeatAt(Warehouse*, int32_t) override { return 0.0f; }
    float LaneBias(Warehouse*, bool, int32_t) override { return 0.0f; }
};

class FlagshipController : public BaselineController {
public:
    float row_flow[MaxGridRows][2];   
    float col_flow[MaxGridCols][2];   
    uint8_t bots_in_cell[MaxCells];
    uint8_t cell_bot[MaxCells];          
    uint8_t crowd_map[MaxCells];         

    const char* Name() const override { return "FLAGSHIP  grid boids"; }
    const char* Summary() const override { return "Congestion-scored A*, lane polarization, platoon trains, instant sidesteps."; }

    static int32_t LaneSlot(Dir4 dir) {
        if (dir == Dir4::East || dir == Dir4::South) 
            return 0;
        return 1;
    }

    void BeginTick(Warehouse* wh, float dt) override;
    float CongestionCost(const LiveConfig* live, int32_t crowd) const;
    int32_t CrowdAround(Warehouse* wh, Bot* bot, int32_t cell);
    float StepCost(Warehouse* wh, Bot* bot, int32_t from, int32_t to, int32_t prev_dir, int32_t goal) override;
    bool CanEnter(Warehouse* wh, Bot* bot, int32_t cell, int32_t from_cell) override;
    float ScoreStep(Warehouse* wh, Bot* bot, int32_t from, int32_t to);
    void RefineAhead(Warehouse* wh, Bot* bot) override;
    void OnSegmentStart(Warehouse* wh, Bot*, int32_t from, int32_t to) override;
    float HeatAt(Warehouse*, int32_t cell) override;
    float LaneBias(Warehouse*, bool is_row, int32_t index) override;
};

bool BotIsStationary(const Bot* bot);
bool SamePlatoon(const Bot* a, const Bot* b);
void TakeCellOwnership(Warehouse* wh, Bot* bot, int32_t cell);
void BlockAndReroute(Warehouse* wh, Bot* bot, int32_t blocked_cell);
void ReleaseCell(Warehouse* wh, Bot* bot, int32_t cell);
void ClearBlockedMemory(Bot* bot);
void RefreshCellOrder(Warehouse* wh, int32_t cell);
void ReleaseDueOrders(Warehouse* wh);
int32_t FindBestOrder(Warehouse* wh, Bot* bot);
void ClaimOrder(Warehouse* wh, Bot* bot, int32_t order_index, bool front);
int32_t BotCapacity(const Warehouse* wh);
void ScanOnTheWay(Warehouse* wh, Bot* bot);
void BotStartTrip(Warehouse* wh, Bot* bot);
bool RequestVacate(Warehouse* wh, Bot* blocker, int32_t asker_cell, int32_t depth);
bool ForcedSidestep(Warehouse* wh, Bot* bot);
void YieldTick(Warehouse* wh, Bot* bot, int32_t blocked_cell, float dt);
void BeginSegment(Warehouse* wh, Bot* bot, int32_t next, float start_progress);
MoveResult BotAdvance(Warehouse* wh, Bot* bot, float dt);
void BotFinishPickup(Warehouse* wh, Bot* bot);
int32_t FindCargoForCell(Warehouse* wh, Bot* bot, int32_t cell);
void BotFinishDropoff(Warehouse* wh, Bot* bot);
int32_t FindParkingCell(Warehouse* wh, Bot* bot);
void BotIdleThink(Warehouse* wh, Bot* bot);
void BotThink(Warehouse* wh, Bot* bot, float dt);
Warehouse* BuildWarehouse(Arena* arena, const Scenario* scenario, const LiveConfig* live, SimSide side);
void WarehouseTick(Warehouse* wh, float dt);

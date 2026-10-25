// simulation.cpp
#include "simulation.hpp"

bool GridAStar::Find(Warehouse* wh, Bot* bot, int32_t start, int32_t goal, int32_t* out_path, int32_t max_length, int32_t* out_length, bool respect_memory) {
    const int32_t cols = wh->cols;
    const int32_t rows = wh->rows;
    Controller* controller = wh->controller;

    epoch++;
    heap_count = 0;
    sequence = 0;

    seen_epoch[start] = epoch;
    g_cost[start] = 0.0f;
    parent[start] = -1;
    int32_t start_h = Manhattan(start, goal, cols);
    HeapPush(start, (float)start_h, start_h);

    bool found = false;
    while (heap_count > 0) {
        int32_t current = HeapPop();
        if (closed_epoch[current] == epoch) continue;
        closed_epoch[current] = epoch;

        if (current == goal) { found = true; break; }

        int32_t prev_dir = DirIndex(bot->dir);
        if (parent[current] >= 0) prev_dir = DirIndex(DirBetween(parent[current], current, cols));

        for (int32_t k = 0; k < 4; k++) {
            int32_t d = (k + (int32_t)(bot->tiebreak & 3u)) & 3;
            int32_t neighbor = NeighborCell(current, DirFromIndex(d), cols, rows);
            if (neighbor < 0) continue;
            if (closed_epoch[neighbor] == epoch) continue;
            if (respect_memory && neighbor != goal && IsBlockedInMemory(wh, bot, neighbor)) continue;

            float step = controller->StepCost(wh, bot, current, neighbor, prev_dir, goal);
            float candidate = g_cost[current] + step;
            if (seen_epoch[neighbor] != epoch || candidate < g_cost[neighbor]) {
                seen_epoch[neighbor] = epoch;
                g_cost[neighbor] = candidate;
                parent[neighbor] = current;
                int32_t h = Manhattan(neighbor, goal, cols);
                HeapPush(neighbor, candidate + (float)h, h);
            }
        }
    }

    if (!found) return false;

    int32_t length = 0;
    for (int32_t cell = goal; cell >= 0; cell = parent[cell]) length++;
    if (length > max_length) return false;

    int32_t write = length - 1;
    for (int32_t cell = goal; cell >= 0; cell = parent[cell]) out_path[write--] = cell;
    *out_length = length;
    return true;
}

bool BaselineController::CanEnter(Warehouse* wh, Bot* bot, int32_t cell, int32_t) { 
    return CellIsFree(wh, bot, cell); 
}

void BaselineController::TakeCell(Warehouse* wh, Bot* bot, int32_t cell) { 
    TakeCellOwnership(wh, bot, cell); 
}

void BaselineController::OnBlocked(Warehouse* wh, Bot* bot, int32_t blocked_cell) { 
    BlockAndReroute(wh, bot, blocked_cell); 
}

void FlagshipController::BeginTick(Warehouse* wh, float dt) {
    const LiveConfig* live = wh->live;

    float keep = sdl::expf(-dt / sdl::max(live->lane_memory_seconds, 0.1f));
    for (int32_t r = 0; r < wh->rows; r++) { row_flow[r][0] *= keep; row_flow[r][1] *= keep; }
    for (int32_t c = 0; c < wh->cols; c++) { col_flow[c][0] *= keep; col_flow[c][1] *= keep; }

    sdl::memset(bots_in_cell, 0, (size_t)(wh->cols * wh->rows));
    sdl::memset(cell_bot, 0, (size_t)(wh->cols * wh->rows));
    for (int32_t b = 0; b < wh->bot_count; b++) {
        Bot* bot = &wh->bots[b];
        if (bot->retired) 
            continue;
        int32_t at = bot->cell;
        if (bot->moving && bot->progress >= 0.5f) at = bot->next_cell;
        if (bots_in_cell[at] < 255) bots_in_cell[at]++;
        cell_bot[at] = (uint8_t)(bot->id + 1);
    }

    if (live->debug_overlay) {
        for (int32_t cell = 0; cell < wh->cols * wh->rows; cell++) {
            int32_t sum = 0;
            int32_t col = CellCol(cell, wh->cols), row = CellRow(cell, wh->cols);

            int32_t radius = live->crowd_radius;
            for (int32_t dy = -radius; dy <= radius; dy++) {
                for (int32_t dx = -radius; dx <= radius; dx++) {
                    int32_t c2 = col + dx, r2 = row + dy;
                    if (c2 < 0 || r2 < 0 || c2 >= wh->cols || r2 >= wh->rows) continue;
                    sum += bots_in_cell[CellIndex(c2, r2, wh->cols)];
                }
            }

            crowd_map[cell] = (uint8_t)sdl::min(sum, 255);
        }
    }
}

float FlagshipController::CongestionCost(const LiveConfig* live, int32_t crowd) const {
    return live->congestion_weight * CongestionScale * (float)(crowd * crowd);
}


int32_t FlagshipController::CrowdAround(Warehouse* wh, Bot* bot, int32_t cell) {
    const LiveConfig* live = wh->live; // <-- Added this line
    int32_t col = CellCol(cell, wh->cols), row = CellRow(cell, wh->cols);
    int32_t crowd = 0;
    int32_t radius = live->crowd_radius;
    for (int32_t dy = -radius; dy <= radius; dy++) {
        for (int32_t dx = -radius; dx <= radius; dx++) {
            int32_t c2 = col + dx, r2 = row + dy;
            if (c2 < 0 || r2 < 0 || c2 >= wh->cols || r2 >= wh->rows) continue;
            int32_t index = CellIndex(c2, r2, wh->cols);
            if (bots_in_cell[index] == 0) continue;
            int32_t other = (int32_t)cell_bot[index] - 1;
            if (other == bot->id) continue;
            if (SamePlatoon(bot, &wh->bots[other])) continue;
            crowd += bots_in_cell[index];
        }
    }
    return crowd;
}


float FlagshipController::StepCost(Warehouse* wh, Bot* bot, int32_t from, int32_t to, int32_t prev_dir, int32_t goal) {
    const LiveConfig* live = wh->live;
    float cost = 1.0f;
    Dir4 dir = DirBetween(from, to, wh->cols);

    if (to != goal) {
        if (live->use_congestion) 
            cost += CongestionCost(live, CrowdAround(wh, bot, to));
        uint8_t owner = wh->cell_owner[to];
        if (owner != 0 && (int32_t)owner - 1 != bot->id) {
            if (BotIsStationary(&wh->bots[owner - 1])) cost += PENALTY_STATIONARY_BOT;
        }
    }

    if (live->use_lanes) {
        float same, opposite;
        int32_t slot = LaneSlot(dir);
        if (DirIsHorizontal(dir)) {
            int32_t row = CellRow(from, wh->cols);
            same = row_flow[row][slot];
            opposite = row_flow[row][1 - slot];
        } else {
            int32_t col = CellCol(from, wh->cols);
            same = col_flow[col][slot];
            opposite = col_flow[col][1 - slot];
        }
        cost += live->lane_weight * opposite / (same + opposite + 1.0f);
    }

    if (prev_dir >= 0 && prev_dir != DirIndex(dir)) 
        cost += live->turn_penalty;
    return cost;
}

bool FlagshipController::CanEnter(Warehouse* wh, Bot* bot, int32_t cell, int32_t from_cell) {
    if (CellIsFree(wh, bot, cell)) 
        return true;

    const LiveConfig* live = wh->live;
    if (!live->use_platoons)
        return false;

    uint8_t owner = wh->cell_owner[cell];
    if (owner == 0 || (int32_t)owner - 1 == bot->id) 
        return false;
    Bot* leader = &wh->bots[owner - 1];
    if (!SamePlatoon(bot, leader)) 
        return false;
    if (!leader->moving || leader->cell != cell) 
        return false;
    if (leader->progress < live->platoon_follow_progress) 
        return false;
    if (DirBetween(from_cell, cell, wh->cols) != leader->dir) 
        return false;
    return true;
}

float FlagshipController::ScoreStep(Warehouse* wh, Bot* bot, int32_t from, int32_t to) {
    float score = StepCost(wh, bot, from, to, DirIndex(bot->dir), bot->target_cell) - 1.0f;
    if (!CanEnter(wh, bot, to, from)) 
        score += PENALTY_OCCUPIED_CELL;
    return score;
}

void FlagshipController::RefineAhead(Warehouse* wh, Bot* bot) {
    const LiveConfig* live = wh->live;
    if (!live->use_sidestep) 
        return;

    int32_t index = bot->path_index;
    if (index + 2 >= bot->path_length) 
        return;
    int32_t here = bot->next_cell;
    int32_t planned = bot->path[index + 2];
    int32_t goal = bot->target_cell;
    if (planned == goal) 
        return;

    bool occupied = !CanEnter(wh, bot, planned, here);

    int32_t radius = live->crowd_radius;
    int32_t area = (radius * 2 + 1) * (radius * 2 + 1);
    int32_t trigger = live->auto_crowd_trigger ? (int32_t)(area * 0.44f) : live->crowd_trigger;
    bool crowded = CrowdAround(wh, bot, planned) >= trigger;

    if (!occupied && !crowded)
        return;

    float planned_score = ScoreStep(wh, bot, here, planned);
    int32_t best = planned;
    float best_score = planned_score;
    int32_t here_distance = Manhattan(here, goal, wh->cols);

    for (int32_t d = 0; d < 4; d++) {
        int32_t candidate = NeighborCell(here, DirFromIndex(d), wh->cols, wh->rows);
        if (candidate < 0 || candidate == planned || candidate == bot->cell) 
            continue;
        if (IsBlockedInMemory(wh, bot, candidate)) 
            continue;
        if (!CanEnter(wh, bot, candidate, here)) 
            continue;

        bool productive = Manhattan(candidate, goal, wh->cols) < here_distance;
        if (!productive && !occupied) 
            continue; 
        float score = ScoreStep(wh, bot, here, candidate);
        if (!productive) 
            score += PENALTY_UNPRODUCTIVE_LATERAL;
        if (score + SCORE_IMPROVEMENT_THRESHOLD < best_score) { best = candidate; best_score = score; }
    }
    if (best == planned) 
        return;

    int32_t tail[MaxPathLength];
    int32_t tail_length = 0;
    if (!wh->astar->Find(wh, bot, best, goal, tail, MaxPathLength, &tail_length, true))
        return;
    if (index + 2 + tail_length > MaxPathLength) 
        return;

    for (int32_t i = 0; i < tail_length; i++) 
        for (int32_t k = 0; k <= index + 1; k++) 
            if (tail[i] == bot->path[k])
                return;
        
    for (int32_t i = 0; i < tail_length; i++) bot->path[index + 2 + i] = tail[i];
    bot->path_length = index + 2 + tail_length;
    wh->stats.sidesteps++;
}

void FlagshipController::OnSegmentStart(Warehouse* wh, Bot*, int32_t from, int32_t to) {
    Dir4 dir = DirBetween(from, to, wh->cols);
    if (DirIsHorizontal(dir)) 
        row_flow[CellRow(from, wh->cols)][LaneSlot(dir)] += 1.0f;
    else 
        col_flow[CellCol(from, wh->cols)][LaneSlot(dir)] += 1.0f;
}

float FlagshipController::HeatAt(Warehouse*, int32_t cell) { return (float)crowd_map[cell]; }

float FlagshipController::LaneBias(Warehouse*, bool is_row, int32_t index) {
    float a, b;
    if (is_row) { a = row_flow[index][0]; b = row_flow[index][1]; }
    else        { a = col_flow[index][0]; b = col_flow[index][1]; }
    return (a - b) / (a + b + 0.5f);
}

bool BotIsStationary(const Bot* bot) {
    if (bot->moving) 
        return false;
    return bot->state == BotState::Picking || bot->state == BotState::Dropping || bot->state == BotState::Idle;
}

bool SamePlatoon(const Bot* a, const Bot* b) {
    return a->state == BotState::ToDropoff && b->state == BotState::ToDropoff && a->target_cell == b->target_cell;
}

void TakeCellOwnership(Warehouse* wh, Bot* bot, int32_t cell) {
    uint8_t owner = wh->cell_owner[cell];
    if (owner != 0 && (int32_t)owner - 1 != bot->id)
        wh->bots[owner - 1].released_from = true;
    wh->cell_owner[cell] = (uint8_t)(bot->id + 1);
}

void BlockAndReroute(Warehouse* wh, Bot* bot, int32_t blocked_cell) {
    if (blocked_cell != bot->target_cell) 
        MarkBlocked(wh, bot, blocked_cell);
    bot->wait_ticks = 0;
    bot->wait_accum = 0.0f;
    bot->reroutes++;
    wh->stats.reroutes++;

    uint32_t h = (uint32_t)(bot->id + 1) * 2654435761u ^ (uint32_t)bot->reroutes * 2246822519u;
    h ^= h >> 15;
    bot->tiebreak = h;
    PlanPath(wh, bot, bot->target_cell);
}

bool CellIsFree(const Warehouse* wh, const Bot* bot, int32_t cell) {
    int32_t owner = (int32_t)wh->cell_owner[cell];
    if (owner != 0 && owner - 1 != bot->id) return false;
    if (wh->time < wh->cell_hold_until[cell]) return false;
    return true;
}

void ReleaseCell(Warehouse* wh, Bot* bot, int32_t cell) {
    if ((int32_t)wh->cell_owner[cell] == bot->id + 1) {
        wh->cell_owner[cell] = 0;
        wh->cell_hold_until[cell] = wh->time + wh->live->clear_hold_seconds;
    }
}

bool IsBlockedInMemory(const Warehouse* wh, const Bot* bot, int32_t cell) {
    for (int32_t i = 0; i < MaxBlockedMemory; i++) {
        if (bot->blocked_cell[i] == cell && wh->time < bot->blocked_until[i]) return true;
    }
    return false;
}

void MarkBlocked(Warehouse* wh, Bot* bot, int32_t cell) {
    int32_t slot = -1;
    for (int32_t i = 0; i < MaxBlockedMemory; i++) {
        if (bot->blocked_cell[i] == cell && bot->blocked_until[i] > 0.0f) { slot = i; break; }
    }
    if (slot < 0) {
        slot = bot->blocked_next_slot;
        bot->blocked_next_slot = (bot->blocked_next_slot + 1) % MaxBlockedMemory;
    }
    bot->blocked_cell[slot] = cell;
    bot->blocked_until[slot] = wh->time + wh->live->blocked_memory_seconds;
}

void ClearBlockedMemory(Bot* bot) {
    for (int32_t i = 0; i < MaxBlockedMemory; i++) bot->blocked_until[i] = 0.0f;
}

bool PlanPath(Warehouse* wh, Bot* bot, int32_t goal) {
    int32_t length = 0;
    bool ok = wh->astar->Find(wh, bot, bot->cell, goal, bot->path, MaxPathLength, &length, true);
    if (!ok) {
        ClearBlockedMemory(bot);
        ok = wh->astar->Find(wh, bot, bot->cell, goal, bot->path, MaxPathLength, &length, false);
    }
    bot->target_cell = goal;
    bot->has_target = true;
    bot->path_index = 0;
    if (!ok) {
        bot->path[0] = bot->cell;
        length = 1;
    }
    bot->path_length = length;
    return ok;
}

void RefreshCellOrder(Warehouse* wh, int32_t cell) {
    wh->cell_order[cell] = 0;
    for (int32_t o = 0; o < wh->next_release; o++) {
        if (wh->orders[o].state == OrderState::Available && wh->scenario->orders[o].pick_cell == cell) {
            wh->cell_order[cell] = o + 1;
            return;
        }
    }
}

void ReleaseDueOrders(Warehouse* wh) {
    while (wh->next_release < wh->order_count && wh->scenario->orders[wh->next_release].release_time <= wh->time) {
        int32_t o = wh->next_release++;
        wh->orders[o].state = OrderState::Available;
        int32_t cell = wh->scenario->orders[o].pick_cell;
        if (wh->cell_order[cell] == 0) wh->cell_order[cell] = o + 1;
    }
}

int32_t FindBestOrder(Warehouse* wh, Bot* bot) {
    int32_t best = -1;
    int32_t best_distance = 0x7fffffff;
    for (int32_t o = 0; o < wh->next_release; o++) {
        if (wh->orders[o].state != OrderState::Available) continue;
        if (wh->live->assign_policy == AssignPolicy::Oldest) return o;
        int32_t distance = Manhattan(bot->cell, wh->scenario->orders[o].pick_cell, wh->cols);
        if (distance < best_distance) { best_distance = distance; best = o; }
    }
    return best;
}

void ClaimOrder(Warehouse* wh, Bot* bot, int32_t order_index, bool front) {
    OrderRuntime* order = &wh->orders[order_index];
    order->state = OrderState::Claimed;
    order->carrier_plus1 = bot->id + 1;
    RefreshCellOrder(wh, wh->scenario->orders[order_index].pick_cell);

    if (front) {
        for (int32_t i = bot->pickup_count; i > 0; i--) bot->pickups[i] = bot->pickups[i - 1];
        bot->pickups[0] = order_index + 1;
    } else {
        bot->pickups[bot->pickup_count] = order_index + 1;
    }
    bot->pickup_count++;
}

int32_t BotCapacity(const Warehouse* wh) { return sdl::clamp(wh->live->bot_capacity, 1, MaxCargo); }

void ScanOnTheWay(Warehouse* wh, Bot* bot) {
    const LiveConfig* live = wh->live;
    if (!live->pickup_on_the_way) return;
    if (bot->state != BotState::ToPickup && bot->state != BotState::ToDropoff) return;
    if (bot->cargo_count + bot->pickup_count >= BotCapacity(wh)) return;

    int32_t detour = sdl::clamp(live->pickup_detour, 0, 3);
    int32_t best_cell = -1;
    int32_t best_score = 0x7fffffff;
    for (int32_t k = bot->path_index; k < bot->path_length; k++) {
        int32_t col = CellCol(bot->path[k], wh->cols);
        int32_t row = CellRow(bot->path[k], wh->cols);
        for (int32_t dy = -detour; dy <= detour; dy++) {
            for (int32_t dx = -detour; dx <= detour; dx++) {
                int32_t off = sdl::abs(dx) + sdl::abs(dy);
                if (off > detour) continue;
                int32_t c2 = col + dx, r2 = row + dy;
                if (c2 < 0 || r2 < 0 || c2 >= wh->cols || r2 >= wh->rows) continue;
                int32_t cell = CellIndex(c2, r2, wh->cols);
                if (wh->cell_order[cell] == 0) continue;
                int32_t score = (k - bot->path_index) + 2 * off;
                if (score < best_score) { best_score = score; best_cell = cell; }
            }
        }
    }
    if (best_cell < 0) return;

    int32_t order_index = wh->cell_order[best_cell] - 1;
    ClaimOrder(wh, bot, order_index, true);
    bot->state = BotState::ToPickup;
    PlanPath(wh, bot, best_cell);
    wh->stats.on_the_way_pickups++;
}

void BotStartTrip(Warehouse* wh, Bot* bot) {
    bot->tiebreak = 0; 
    bot->best_distance_plus1 = 0;
    if (bot->pickup_count > 0) {
        bot->state = BotState::ToPickup;
        PlanPath(wh, bot, wh->scenario->orders[bot->pickups[0] - 1].pick_cell);
    } else if (bot->cargo_count > 0) {
        int32_t best_station = -1;
        int32_t best_distance = 0x7fffffff;
        for (int32_t i = 0; i < bot->cargo_count; i++) {
            int32_t station = StationCell(wh->scenario->orders[bot->cargo[i] - 1].station_col, wh->cols, wh->rows);
            int32_t distance = Manhattan(bot->cell, station, wh->cols);
            if (distance < best_distance) { best_distance = distance; best_station = station; }
        }
        bot->state = BotState::ToDropoff;
        PlanPath(wh, bot, best_station);
    } else {
        bot->state = BotState::Idle;
        bot->has_target = false;
        bot->path_length = 0;
        return;
    }
    ScanOnTheWay(wh, bot);
}

bool RequestVacate(Warehouse* wh, Bot* blocker, int32_t asker_cell, int32_t depth) {
    if (blocker->state != BotState::Idle || blocker->moving) return false;

    constexpr int32_t PreferredOrder[4] = { (int32_t)Dir4::North, (int32_t)Dir4::East, (int32_t)Dir4::West, (int32_t)Dir4::South };
    for (int32_t i = 0; i < 4; i++) {
        int32_t neighbor = NeighborCell(blocker->cell, DirFromIndex(PreferredOrder[i]), wh->cols, wh->rows);
        if (neighbor < 0 || neighbor == asker_cell) continue;
        if (!CellIsFree(wh, blocker, neighbor)) continue;
        blocker->state = BotState::Retreat;
        PlanPath(wh, blocker, neighbor);
        return true;
    }
    if (depth <= 0) return false;
    for (int32_t i = 0; i < 4; i++) {
        int32_t neighbor = NeighborCell(blocker->cell, DirFromIndex(PreferredOrder[i]), wh->cols, wh->rows);
        if (neighbor < 0 || neighbor == asker_cell) continue;
        int32_t owner = (int32_t)wh->cell_owner[neighbor];
        if (owner == 0) continue;
        if (RequestVacate(wh, &wh->bots[owner - 1], blocker->cell, depth - 1)) return true;
    }
    return false;
}

bool ForcedSidestep(Warehouse* wh, Bot* bot) {
    int32_t free_neighbors[4];
    int32_t free_count = 0;
    for (int32_t d = 0; d < 4; d++) {
        int32_t neighbor = NeighborCell(bot->cell, DirFromIndex(d), wh->cols, wh->rows);
        if (neighbor < 0) continue;
        if (!CellIsFree(wh, bot, neighbor)) continue;
        free_neighbors[free_count++] = neighbor;
    }
    if (free_count == 0) return false;

    int32_t pick = (int32_t)((wh->tick_index / 7u + (uint32_t)bot->id) % (uint32_t)free_count);
    bot->state = BotState::Retreat; 
    bot->giving_way = true;
    bot->best_distance_plus1 = 0;
    PlanPath(wh, bot, free_neighbors[pick]);
    bot->stuck_count = 0;
    wh->stats.unstick_moves++;
    return true;
}

void YieldTick(Warehouse* wh, Bot* bot, int32_t blocked_cell, float dt) {
    const LiveConfig* live = wh->live;
    float tick_length = sdl::max(live->yield_tick_seconds, 0.01f);

    bot->speed = 0.0f;
    bot->wait_seconds += dt;
    wh->stats.wait_seconds += dt;
    bot->wait_accum += dt;
    while (bot->wait_accum >= tick_length) {
        bot->wait_accum -= tick_length;
        bot->wait_ticks++;
        wh->stats.yield_ticks++;
    }
    if (bot->wait_ticks > live->wait_tick_limit) {
        int32_t owner = (int32_t)wh->cell_owner[blocked_cell];
        bool blocked_by_bot = owner != 0 && owner - 1 != bot->id;
        if (blocked_by_bot) RequestVacate(wh, &wh->bots[owner - 1], bot->cell, 2);
        wh->controller->OnBlocked(wh, bot, blocked_cell);

        bot->stuck_count++;
        int32_t threshold = StuckRerouteLimit;
        if (blocked_by_bot) { if (bot->id < owner - 1) threshold += 3; }
        else threshold += bot->id % 3;
        if (bot->stuck_count >= threshold) ForcedSidestep(wh, bot);
    }
}

void BeginSegment(Warehouse* wh, Bot* bot, int32_t next, float start_progress) {
    bot->moving = true;
    bot->next_cell = next;
    bot->progress = start_progress;
    bot->ahead_reserved = false;
    bot->released_from = false;
    bot->wait_ticks = 0;
    bot->wait_accum = 0.0f;
    int32_t distance_plus1 = Manhattan(next, bot->target_cell, wh->cols) + 1;
    if (bot->best_distance_plus1 == 0 || distance_plus1 < bot->best_distance_plus1) {
        bot->best_distance_plus1 = distance_plus1;
        bot->stuck_count = 0;
    }
    bot->dir = DirBetween(bot->cell, next, wh->cols);
    wh->controller->OnSegmentStart(wh, bot, bot->cell, next);
}

MoveResult BotAdvance(Warehouse* wh, Bot* bot, float dt) {
    const LiveConfig* live = wh->live;
    Controller* controller = wh->controller;

    if (!bot->moving) {
        if (wh->time < bot->hold_until) { 
            bot->speed = 0.0f;
            bot->wait_seconds += dt;
            wh->stats.wait_seconds += dt;
            return MoveResult::Waiting;
        }
        if (bot->path_index >= bot->path_length - 1) {
            bot->speed = 0.0f;
            return MoveResult::Arrived;
        }
        int32_t next = bot->path[bot->path_index + 1];
        if (!controller->CanEnter(wh, bot, next, bot->cell)) {
            YieldTick(wh, bot, next, dt);
            return MoveResult::Waiting;
        }
        controller->TakeCell(wh, bot, next);
        BeginSegment(wh, bot, next, 0.0f);
    }

    int32_t index = bot->path_index;
    bool has_after = index + 2 < bot->path_length;

    if (has_after && !bot->ahead_reserved) {
        float stop_distance = bot->speed * bot->speed / (2.0f * live->decel);
        if (1.0f - bot->progress <= stop_distance + live->reserve_margin) {
            controller->RefineAhead(wh, bot);
            has_after = index + 2 < bot->path_length;
            if (has_after) {
                int32_t ahead = bot->path[index + 2];
                if (ahead != bot->cell && controller->CanEnter(wh, bot, ahead, bot->next_cell)) {
                    controller->TakeCell(wh, bot, ahead);
                    bot->ahead_reserved = true;
                }
            }
        }
    }

    float end_speed = 0.0f;
    if (has_after && bot->ahead_reserved) {
        Dir4 now = bot->dir;
        Dir4 then = DirBetween(bot->next_cell, bot->path[index + 2], wh->cols);
        if (now != then) end_speed = live->turn_speed;
        else end_speed = live->max_speed;
    }
    float remaining = 1.0f - bot->progress;
    float v_brake = sdl::sqrtf(end_speed * end_speed + 2.0f * live->decel * remaining);
    float v_cap = sdl::min(live->max_speed, v_brake);

    float v;
    if (bot->speed < v_cap) v = sdl::min(bot->speed + live->accel * dt, v_cap);
    else v = sdl::max(v_cap, bot->speed - live->decel * dt);

    float step = v * dt;
    bot->speed = v;
    bot->progress += step;
    bot->distance_cells += step;
    wh->stats.distance_cells += step;

    if (!bot->released_from && bot->progress >= live->body_clear_progress) {
        ReleaseCell(wh, bot, bot->cell);
        bot->released_from = true;
    }

    if (bot->progress >= 1.0f) {
        float overshoot = bot->progress - 1.0f;
        if (!bot->released_from) ReleaseCell(wh, bot, bot->cell);

        bot->cell = bot->next_cell;
        bot->path_index++;
        bot->moving = false;
        bot->progress = 0.0f;

        if (bot->ahead_reserved) {
            BeginSegment(wh, bot, bot->path[bot->path_index + 1], overshoot);
        } else {
            bot->speed = 0.0f;
            return MoveResult::Arrived;
        }
    }
    return MoveResult::Moving;
}

void BotFinishPickup(Warehouse* wh, Bot* bot) {
    int32_t order_index = bot->pickups[0] - 1;
    for (int32_t i = 1; i < bot->pickup_count; i++) bot->pickups[i - 1] = bot->pickups[i];
    bot->pickup_count--;
    bot->pickups[bot->pickup_count] = 0;

    bot->cargo[bot->cargo_count++] = order_index + 1;
    wh->orders[order_index].state = OrderState::Carried;
    BotStartTrip(wh, bot);
}

int32_t FindCargoForCell(Warehouse* wh, Bot* bot, int32_t cell) {
    for (int32_t i = 0; i < bot->cargo_count; i++) {
        int32_t station = StationCell(wh->scenario->orders[bot->cargo[i] - 1].station_col, wh->cols, wh->rows);
        if (station == cell) return i;
    }
    return -1;
}

void BotFinishDropoff(Warehouse* wh, Bot* bot) {
    int32_t slot = FindCargoForCell(wh, bot, bot->cell);
    if (slot >= 0) {
        int32_t order_index = bot->cargo[slot] - 1;
        for (int32_t i = slot + 1; i < bot->cargo_count; i++) bot->cargo[i - 1] = bot->cargo[i];
        bot->cargo_count--;
        bot->cargo[bot->cargo_count] = 0;

        wh->orders[order_index].state = OrderState::Delivered;
        wh->orders[order_index].delivered_time = wh->time;
        wh->delivered++;
        wh->station_delivered[CellCol(bot->cell, wh->cols)]++;
        wh->stats.latency_sum += wh->time - wh->scenario->orders[order_index].release_time;
        bot->orders_done++;
    }

    if (FindCargoForCell(wh, bot, bot->cell) >= 0) {
        bot->action_timer = wh->live->dropoff_seconds;
        return;
    }
    BotStartTrip(wh, bot);
}

int32_t FindParkingCell(Warehouse* wh, Bot* bot) {
    uint8_t crate_here[MaxGridCols] = {};
    for (int32_t o = 0; o < wh->next_release; o++) {
        OrderState state = wh->orders[o].state;
        if (state != OrderState::Available && state != OrderState::Claimed) continue;
        int32_t pick = wh->scenario->orders[o].pick_cell;
        if (CellRow(pick, wh->cols) == 0) crate_here[CellCol(pick, wh->cols)] = 1;
    }

    int32_t bot_col = CellCol(bot->cell, wh->cols);
    for (int32_t spread = 0; spread < wh->cols; spread++) {
        for (int32_t side = 0; side < 2; side++) {
            int32_t col = bot_col + (side == 0 ? spread : -spread);
            if (col < 0 || col >= wh->cols) continue;
            int32_t cell = CellIndex(col, 0, wh->cols);
            if (!CellIsFree(wh, bot, cell)) continue;
            if (crate_here[col]) continue; 
            return cell;
        }
    }
    return -1;
}

void BotIdleThink(Warehouse* wh, Bot* bot) {
    int32_t order_index = FindBestOrder(wh, bot);
    if (order_index >= 0) {
        ClaimOrder(wh, bot, order_index, false);
        BotStartTrip(wh, bot);
        return;
    }
    if (wh->live->retire_idle_bots && wh->next_release >= wh->order_count) {
        ReleaseCell(wh, bot, bot->cell);
        bot->retired = true;
        return;
    }
    if (CellRow(bot->cell, wh->cols) != 0) {
        int32_t park = FindParkingCell(wh, bot);
        if (park >= 0 && park != bot->cell) {
            bot->state = BotState::Retreat;
            PlanPath(wh, bot, park);
        }
    }
}

void BotThink(Warehouse* wh, Bot* bot, float dt) {
    const LiveConfig* live = wh->live;

    switch (bot->state) {
        case BotState::Idle: {
            bot->speed = 0.0f;
            BotIdleThink(wh, bot);
        } break;

        case BotState::ToPickup:
        case BotState::ToDropoff:
        case BotState::Retreat: {
            if (!bot->moving && wh->cell_order[bot->cell] != 0) ScanOnTheWay(wh, bot);

            if (bot->state == BotState::Retreat && !bot->moving && bot->pickup_count == 0 && bot->cargo_count == 0) {
                int32_t order_index = FindBestOrder(wh, bot);
                if (order_index >= 0) {
                    ClaimOrder(wh, bot, order_index, false);
                    BotStartTrip(wh, bot);
                    break;
                }
                if (!CellIsFree(wh, bot, bot->target_cell) && bot->cell != bot->target_cell) {
                    int32_t park = FindParkingCell(wh, bot);
                    if (park >= 0 && park != bot->target_cell) PlanPath(wh, bot, park);
                }
            }

            MoveResult result = BotAdvance(wh, bot, dt);
            if (result == MoveResult::Arrived) {
                if (bot->cell != bot->target_cell) {
                    PlanPath(wh, bot, bot->target_cell); 
                } else if (bot->state == BotState::ToPickup) {
                    bot->state = BotState::Picking;
                    bot->action_timer = live->pickup_seconds;
                } else if (bot->state == BotState::ToDropoff) {
                    bot->state = BotState::Dropping;
                    bot->action_timer = live->dropoff_seconds;
                } else {
                    if (bot->pickup_count > 0 || bot->cargo_count > 0) {
                        if (bot->giving_way) bot->hold_until = wh->time + GiveWaySeconds;
                        bot->giving_way = false;
                        BotStartTrip(wh, bot);
                    } else {
                        bot->giving_way = false;
                        bot->state = BotState::Idle;
                    }
                }
            }
        } break;

        case BotState::Picking: {
            bot->speed = 0.0f;
            bot->action_timer -= dt;
            if (bot->action_timer <= 0.0f) BotFinishPickup(wh, bot);
        } break;

        case BotState::Dropping: {
            bot->speed = 0.0f;
            bot->action_timer -= dt;
            if (bot->action_timer <= 0.0f) BotFinishDropoff(wh, bot);
        } break;
    }

    Vec2 center = CellCenter(bot->cell, wh->cols);
    if (bot->moving) {
        center += Vec2((float)DirDX[DirIndex(bot->dir)], (float)DirDY[DirIndex(bot->dir)]) * bot->progress;
    }
    bot->pos = center;

    float delta = sdl::wrap_turns_signed(DirTurns(bot->dir) - bot->heading_turns);
    bot->heading_turns += delta * (1.0f - sdl::expf(-HEADING_DECAY_RATE * dt));

    bot->trail_timer += dt;
    if (bot->trail_timer >= TRAIL_EMIT_INTERVAL) {
        bot->trail_timer = 0.0f;
        bot->trail[bot->trail_head] = center;
        bot->trail_head = (bot->trail_head + 1) % TrailLength;
        if (bot->trail_count < TrailLength) bot->trail_count++;
    }
}

Warehouse* BuildWarehouse(Arena* arena, const Scenario* scenario, const LiveConfig* live, SimSide side) {
    Warehouse* wh = PushStruct<Warehouse>(arena);
    wh->arena = arena;
    wh->scenario = scenario;
    wh->live = live;
    wh->side = side;
    wh->cols = scenario->cols;
    wh->rows = scenario->rows;

    if (side == SimSide::Flagship) wh->controller = PushObject<FlagshipController>(arena);
    else wh->controller = PushObject<BaselineController>(arena);
    wh->astar = PushStruct<GridAStar>(arena);

    int32_t cell_count = wh->cols * wh->rows;
    wh->cell_owner = PushArray<uint8_t>(arena, (uint64_t)cell_count);
    wh->cell_hold_until = PushArray<float>(arena, (uint64_t)cell_count);
    wh->cell_order = PushArray<int32_t>(arena, (uint64_t)cell_count);

    wh->order_count = scenario->order_count;
    wh->orders = PushArray<OrderRuntime>(arena, (uint64_t)scenario->order_count);

    wh->bot_count = scenario->bot_count;
    wh->bots = PushArray<Bot>(arena, (uint64_t)scenario->bot_count);
    for (int32_t b = 0; b < wh->bot_count; b++) {
        Bot* bot = &wh->bots[b];
        bot->id = b;
        bot->cell = scenario->bot_start_cell[b];
        bot->pos = CellCenter(bot->cell, wh->cols);
        bot->prev_pos = bot->pos;
        bot->heading_turns = 0.0f;
        wh->cell_owner[bot->cell] = (uint8_t)(b + 1);
    }
    return wh;
}

void WarehouseTick(Warehouse* wh, float dt) {
    if (wh->finished) return;

    wh->time += dt;
    wh->tick_index++;
    ReleaseDueOrders(wh);
    wh->controller->BeginTick(wh, dt);

    int32_t count = wh->bot_count;
    int32_t start = (int32_t)(wh->tick_index % (uint32_t)count);
    for (int32_t i = 0; i < count; i++) {
        Bot* bot = &wh->bots[(start + i) % count];
        if (bot->retired) 
            continue;
        bot->prev_pos = bot->pos;
        BotThink(wh, bot, dt);
    }

    if (wh->delivered >= wh->order_count) {
        wh->finished = true;
        wh->finish_time = wh->time;
    }
}

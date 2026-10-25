// renderer.hpp
#pragma once
#include "sdl_wrapper.hpp"
#include "world.hpp"

constexpr SDL_FColor ColorBackground = { 0.040f, 0.046f, 0.065f, 1.0f };

// --- Render & Camera Constants ---
constexpr float ZOOM_GLIDE_RATE = 8.0f;
constexpr float RENDER_TILE_INSET_RATIO = 0.04f;
constexpr float RENDER_TILE_INSET_MIN = 0.6f;
constexpr float RENDER_BAY_X_OFFSET = 0.14f;
constexpr float RENDER_BAY_Y_OFFSET = 0.08f;
constexpr float RENDER_BAY_WIDTH = 0.72f;
constexpr float RENDER_BAY_HEIGHT = 0.52f;
constexpr float RENDER_CRATE_HALF_SIZE = 0.22f;
constexpr float RENDER_CRATE_RING_OFFSET = 0.07f;
constexpr float RENDER_TRAIL_THICKNESS_RATIO = 0.05f;
constexpr float RENDER_TRAIL_THICKNESS_MIN = 0.7f;
constexpr float RENDER_PATH_THICKNESS_SELECTED = 0.045f;
constexpr float RENDER_PATH_THICKNESS_NORMAL = 0.025f;
constexpr float RENDER_BOT_BODY_HALF = 0.37f;
constexpr float RENDER_BOT_LID_HALF = 0.22f;
constexpr float RENDER_BOT_NOSE_TIP_X = 0.34f;
constexpr float RENDER_BOT_NOSE_TIP_Y = 0.34f;
constexpr float RENDER_BOT_NOSE_BASE_X = 0.20f;
constexpr float RENDER_BOT_NOSE_BASE_Y = 0.11f;
constexpr float RENDER_CARGO_HALF_SIZE = 0.075f;
constexpr float RENDER_CARGO_OFFSET_START = -0.12f;
constexpr float RENDER_CARGO_OFFSET_STEP = -0.15f;
constexpr float RENDER_SELECTION_RING_RADIUS = 0.78f;
constexpr float RENDER_SELECTION_OUTLINE_HALF = 0.52f;

class App;

class Camera {
public:
    float x, y;
    float zoom;        
    float zoom_target; 

    inline SDL_FPoint WorldToLogical(Vec2 world_position, float origin_x, float origin_y) const {
        return SDL_FPoint{ world_position.X * zoom + origin_x, world_position.Y * zoom + origin_y };
    }

    inline Vec2 LogicalToWorld(float logical_x, float logical_y, float origin_x, float origin_y) const {
        return Vec2((logical_x - origin_x) / zoom, (logical_y - origin_y) / zoom);
    }

    inline void UpdateZoom(float delta_seconds) {
        zoom = sdl::lerp_exp(zoom, zoom_target, ZOOM_GLIDE_RATE, delta_seconds);
    }
};

class VertexBuffer {
public:
    void Init(Arena* arena, int32_t capacity) {
        data_ = PushArray<SDL_Vertex>(arena, capacity);
        capacity_ = capacity;
        count_ = 0;
    }

    void Clear() { count_ = 0; }

    bool PushBack(SDL_Vertex vertex) {
        if (count_ >= capacity_) 
            return false;
        data_[count_++] = vertex;
        return true;
    }

    int32_t Count() const { return count_; }
    int32_t Capacity() const { return capacity_; }
    SDL_Vertex* Data() { return data_; }

private:
    SDL_Vertex* data_;
    int32_t count_;
    int32_t capacity_;
};

class IndexBuffer {
public:
    void Init(Arena* arena, int32_t capacity) {
        data_ = PushArray<int32_t>(arena, capacity);
        capacity_ = capacity;
        count_ = 0;
    }

    void Clear() { count_ = 0; }

    bool PushBack(int32_t index) {
        if (count_ >= capacity_) 
            return false;
        data_[count_++] = index;
        return true;
    }

    int32_t Count() const { return count_; }
    int32_t Capacity() const { return capacity_; }
    int32_t* Data() { return data_; }

private:
    int32_t* data_;
    int32_t count_;
    int32_t capacity_;
};

class ViewXform {
public:
    float origin_x, origin_y;
    float cell_px; 

    SDL_FPoint Center(float cell_x, float cell_y) const {
        return SDL_FPoint{ (cell_x + 0.5f) * cell_px + origin_x, (cell_y + 0.5f) * cell_px + origin_y };
    }
    SDL_FPoint Corner(float cell_x, float cell_y) const {
        return SDL_FPoint{ cell_x * cell_px + origin_x, cell_y * cell_px + origin_y };
    }
};

inline void PushArcFan(VertexBuffer* vertices, IndexBuffer* indices, SDL_FPoint center, float radius, float start_turns, float end_turns, SDL_FColor color, int32_t segments) {
    if (radius <= 0.0f || segments < 1) 
        return;
    if (vertices->Count() + segments + 2 > vertices->Capacity())
        return;
    if (indices->Count() + segments * 3 > indices->Capacity()) 
        return;

    int32_t center_index = vertices->Count();

    SDL_Vertex vertex;
    vertex.position = center;
    vertex.color = color;
    vertex.tex_coord = SDL_FPoint{0.0f, 0.0f};
    vertices->PushBack(vertex);

    for (int32_t segment = 0; segment <= segments; segment++) {
        float t = (float)segment / (float)segments;
        float angle_turns = start_turns + (end_turns - start_turns) * t;
        vertex.position = SDL_FPoint{ center.x + sdl::cos_turns(angle_turns) * radius, center.y + sdl::sin_turns(angle_turns) * radius };
        vertices->PushBack(vertex);
    }

    for (int32_t segment = 0; segment < segments; segment++) {
        indices->PushBack(center_index);
        indices->PushBack(center_index + 1 + segment);
        indices->PushBack(center_index + 2 + segment);
    }
}

inline void PushQuad(VertexBuffer* vertices, IndexBuffer* indices, SDL_FPoint corner_a, SDL_FPoint corner_b, SDL_FPoint corner_c, SDL_FPoint corner_d, SDL_FColor color) {
    if (vertices->Count() + 4 > vertices->Capacity() 
        || indices->Count() + 6 > indices->Capacity()
        ) return;

    int32_t base_index = vertices->Count();
    SDL_Vertex vertex;
    vertex.color = color;
    vertex.tex_coord = SDL_FPoint{0.0f, 0.0f};

    vertex.position = corner_a; vertices->PushBack(vertex);
    vertex.position = corner_b; vertices->PushBack(vertex);
    vertex.position = corner_c; vertices->PushBack(vertex);
    vertex.position = corner_d; vertices->PushBack(vertex);

    constexpr int32_t quad_winding[6] = { 0, 1, 2, 1, 3, 2 };

    for (int32_t order_index = 0; order_index < 6; order_index++) 
        indices->PushBack(base_index + quad_winding[order_index]);
}

inline void PushThickLine(VertexBuffer* vertices, IndexBuffer* indices, SDL_FPoint from, SDL_FPoint to, float half_thickness, SDL_FColor color_from, SDL_FColor color_to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float length = sdl::sqrtf(dx * dx + dy * dy);
    if (length <= 0.001f) 
        return;
    if (vertices->Count() + 4 > vertices->Capacity() 
        || indices->Count() + 6 > indices->Capacity()
        ) return;

    float normal_x = -dy / length;
    float normal_y = dx / length;
    int32_t base_index = vertices->Count();
    SDL_Vertex vertex;
    vertex.tex_coord = SDL_FPoint{0.0f, 0.0f};

    vertex.color = color_from;
    vertex.position = { from.x + normal_x * half_thickness, from.y + normal_y * half_thickness }; vertices->PushBack(vertex);
    vertex.position = { from.x - normal_x * half_thickness, from.y - normal_y * half_thickness }; vertices->PushBack(vertex);
    vertex.color = color_to;
    vertex.position = { to.x + normal_x * half_thickness, to.y + normal_y * half_thickness }; vertices->PushBack(vertex);
    vertex.position = { to.x - normal_x * half_thickness, to.y - normal_y * half_thickness }; vertices->PushBack(vertex);

    constexpr int32_t quad_winding[6] = { 0, 1, 2, 1, 3, 2 };

    for (int32_t order_index = 0; order_index < 6; order_index++) 
        indices->PushBack(base_index + quad_winding[order_index]);
}

inline void PushRect(VertexBuffer* vertices, IndexBuffer* indices, float x, float y, float width, float height, SDL_FColor color) {
    PushQuad(vertices, indices, SDL_FPoint{x, y}, SDL_FPoint{x + width, y}, SDL_FPoint{x, y + height}, SDL_FPoint{x + width, y + height}, color);
}

inline void PushRotatedRect(VertexBuffer* vertices, IndexBuffer* indices, SDL_FPoint center, float half_length, float half_width, float turns, SDL_FColor color) {
    float fx = sdl::cos_turns(turns), fy = sdl::sin_turns(turns); 
    float rx = -fy, ry = fx;                                      
    SDL_FPoint a = { center.x - fx * half_length - rx * half_width, center.y - fy * half_length - ry * half_width };
    SDL_FPoint b = { center.x + fx * half_length - rx * half_width, center.y + fy * half_length - ry * half_width };
    SDL_FPoint c = { center.x - fx * half_length + rx * half_width, center.y - fy * half_length + ry * half_width };
    SDL_FPoint d = { center.x + fx * half_length + rx * half_width, center.y + fy * half_length + ry * half_width };
    PushQuad(vertices, indices, a, b, c, d, color);
}

inline void PushRectOutline(VertexBuffer* vertices, IndexBuffer* indices, float x, float y, float width, float height, float thickness, SDL_FColor color) {
    PushRect(vertices, indices, x, y, width, thickness, color);
    PushRect(vertices, indices, x, y + height - thickness, width, thickness, color);
    PushRect(vertices, indices, x, y + thickness, thickness, height - thickness * 2.0f, color);
    PushRect(vertices, indices, x + width - thickness, y + thickness, thickness, height - thickness * 2.0f, color);
}

inline void PushTriangle(VertexBuffer* vertices, IndexBuffer* indices, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FColor color) {
    if (vertices->Count() + 3 > vertices->Capacity() 
        || indices->Count() + 3 > indices->Capacity()
        ) return;

    int32_t base_index = vertices->Count();
    SDL_Vertex vertex;
    vertex.color = color;
    vertex.tex_coord = SDL_FPoint{0.0f, 0.0f};
    vertex.position = a; vertices->PushBack(vertex);
    vertex.position = b; vertices->PushBack(vertex);
    vertex.position = c; vertices->PushBack(vertex);
    indices->PushBack(base_index);
    indices->PushBack(base_index + 1);
    indices->PushBack(base_index + 2);
}

inline void PushRingOutline(SDL_Renderer* renderer, SDL_FPoint center, float radius, int32_t segments) {
    SDL_FPoint points[65];
    if (segments > 64) 
        segments = 64;
    for (int32_t i = 0; i <= segments; i++) {
        float turns = (float)i / (float)segments;
        points[i] = { center.x + sdl::cos_turns(turns) * radius, center.y + sdl::sin_turns(turns) * radius };
    }
    SDL_RenderLines(renderer,points, segments + 1);
}

inline SDL_FColor RgbToFColor(colors::Rgb rgb, float alpha = 1.0f) {
    return SDL_FColor{ rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha };
}

inline SDL_FColor MakeColor(float r, float g, float b, float a = 1.0f) { return SDL_FColor{ r, g, b, a }; }
inline SDL_FColor MixColor(SDL_FColor a, SDL_FColor b, float t) {
    return SDL_FColor{ sdl::lerp(a.r, b.r, t), sdl::lerp(a.g, b.g, t), sdl::lerp(a.b, b.b, t), sdl::lerp(a.a, b.a, t) };
}

inline SDL_FColor BotStateColor(const Warehouse* wh, const Bot* bot) {
    switch (bot->state) {
        case BotState::Picking:  return MakeColor(0.40f, 0.88f, 0.52f);
        case BotState::Dropping: return MakeColor(0.36f, 0.62f, 1.00f);
        case BotState::Idle:     return MakeColor(0.46f, 0.49f, 0.57f);
        default: break;
    }
    bool held = wh->time < bot->hold_until;
    if (!bot->moving && (bot->wait_ticks > 0 || bot->wait_accum > 0.0f || held)) {
        float t = sdl::saturate((float)bot->wait_ticks / (float)sdl::max(wh->live->wait_tick_limit + 1, 1));
        return MixColor(MakeColor(1.00f, 0.80f, 0.22f), MakeColor(0.96f, 0.26f, 0.22f), t);
    }
    return MakeColor(0.87f, 0.89f, 0.94f);
}

void DrawWarehouse(App* app, Warehouse* wh, float view_w, float view_h, float alpha);
void RenderAll(App* app, float alpha, float delta_seconds);

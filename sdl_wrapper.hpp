// sdl_wrapper.hpp
#pragma once
#include <SDL3/SDL.h>
#include "base_arena.hpp"

constexpr bool DebugEnabled = true;

namespace sdl {

    inline int snprintf(char* text, size_t maxlen, const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int r = SDL_vsnprintf(text, maxlen, fmt, ap);
        va_end(ap);
        return r;
    }

    inline void log(const char* fmt, ...) {
        if constexpr (DebugEnabled) {
            va_list ap;
            va_start(ap, fmt);
            SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
            va_end(ap);
        }
    }

    inline const char* get_error() { return SDL_GetError(); }
    inline uint64_t str_to_u64(const char* str, char** endptr, int base) { return SDL_strtoull(str, endptr, base); }
    inline uint32_t get_mouse_state(float* x, float* y) { return SDL_GetMouseState(x, y); }

    inline float fabsf(float x) { return SDL_fabsf(x); }
    inline float ceilf(float x) { return SDL_ceilf(x); }
    inline float logf(float x) { return SDL_logf(x); }

    inline void* memset(void* dst, int c, size_t len) { return ::memset(dst, c, len); }
    inline char* strstr(const char* haystack, const char* needle) { return const_cast<char*>(::strstr(haystack, needle)); }

    template <typename T> constexpr T min(T a, T b)
       {
        if (a < b) return a; return b;
         }
    template <typename T> constexpr T max(T a, T b)   { if (a > b) return a; return b; }
    template <typename T> constexpr T clamp(T value, T low, T high) {
      if (value < low) return low;
      if (value > high) return high;
      return value;
    }

    template <typename T> constexpr T abs(T value) { if (value < T(0)) return -value; return value; }
    inline float saturate(float x) { return clamp(x, 0.0f, 1.0f); }
    inline float sqrtf(float x) { return SDL_sqrtf(x); }
    inline float sinf(float x) { return SDL_sinf(x); }
    inline float cosf(float x) { return SDL_cosf(x); }
    inline float atan2f(float y, float x) { return SDL_atan2f(y, x); }
    inline float expf(float x) { return SDL_expf(x); }
    inline float powf(float x, float y) { return SDL_powf(x, y); }
    inline float floorf(float x) { return SDL_floorf(x); }
    inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

    inline float sin_turns(float turns) { return SDL_sinf(TurnsToRadians(turns)); }
    inline float cos_turns(float turns) { return SDL_cosf(TurnsToRadians(turns)); }
    inline float atan2_turns(float rise, float run) { return RadiansToTurns(SDL_atan2f(rise, run)); }

    constexpr float wrap_turns_signed(float turns) {
        while (turns >  TurnHalf) turns -= TurnFull;
        while (turns < -TurnHalf) turns += TurnFull;
        return turns;
    }

    inline float lerp_exp(float from, float to, float decay, float delta_seconds) {
        return to + (from - to) * SDL_expf(-decay * delta_seconds);
    }

    inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return Vec2(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t); }

    inline Vec2 lerp_exp(Vec2 from, Vec2 to, float decay, float delta_seconds) {
        float t = SDL_expf(-decay * delta_seconds);
        return Vec2(to.X + (from.X - to.X) * t, to.Y + (from.Y - to.Y) * t);
    }

    inline int32_t rand_below(uint64_t* state, int32_t count) { return SDL_rand_r(state, count); }
    inline float rand01(uint64_t* state) { return SDL_randf_r(state); }

    inline uint64_t now_ns() {
        uint64_t counter = SDL_GetPerformanceCounter();
        uint64_t frequency = SDL_GetPerformanceFrequency();
        return (counter / frequency) * 1'000'000'000ull + ((counter % frequency) * 1'000'000'000ull) / frequency;
    }

    class Platform {
    public:
        bool Init() { return SDL_Init(SDL_INIT_VIDEO); }
        void Quit() { SDL_Quit(); }
    };

    class Window {
    public:
        SDL_Window* handle;

        bool Create(const char* title, int32_t width, int32_t height, uint64_t flags) {
            handle = SDL_CreateWindow(title, width, height, flags);
            return handle != nullptr;
        }

        void Destroy() {
            if (handle) SDL_DestroyWindow(handle);
            handle = nullptr;
        }

        void GetSize(int32_t* width, int32_t* height) {
            SDL_GetWindowSize(handle, width, height);
        }

        float PixelDensity() {
            float density = SDL_GetWindowPixelDensity(handle);
            if (density > 0.0f) return density;
            return 1.0f;
        }

        float RefreshRate() {
            const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(handle));
            if (mode && mode->refresh_rate > 1.0f) return mode->refresh_rate;
            return 60.0f;
        }
    };

    class Renderer {
    public:
        SDL_Renderer* handle;

        bool Create(Window* window) {
            handle = SDL_CreateRenderer(window->handle, nullptr);
            if (handle) SDL_SetRenderVSync(handle, 1);
            return handle != nullptr;
        }

        void Destroy() {
            if (handle) SDL_DestroyRenderer(handle);
            handle = nullptr;
        }
        void SetDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { SDL_SetRenderDrawColor(handle, r, g, b, a); }
        void SetDrawBlendMode(SDL_BlendMode mode) { SDL_SetRenderDrawBlendMode(handle, mode); }
        void Clear() { SDL_RenderClear(handle); }
        void DrawLine(float x1, float y1, float x2, float y2) { SDL_RenderLine(handle, x1, y1, x2, y2); }
        void DrawLines(const SDL_FPoint* points, int32_t count) { SDL_RenderLines(handle, points, count); }
        void DrawGeometry(SDL_Vertex* vertices, int32_t vertex_count, int32_t* indices, int32_t index_count) {
        SDL_RenderGeometry(handle, nullptr, vertices, vertex_count, indices, index_count);
        }
        void SetTarget(SDL_Texture* target) { SDL_SetRenderTarget(handle, target); }
        void SetScale(float x, float y) { SDL_SetRenderScale(handle, x, y); }
        void DrawTexture(SDL_Texture* texture) { SDL_RenderTexture(handle, texture, nullptr, nullptr); }
        
        void DrawTextureRect(SDL_Texture* texture, float x, float y, float width, float height) {
            SDL_FRect destination = { x, y, width, height };
            SDL_RenderTexture(handle, texture, nullptr, &destination);
        }
        
        void Present() { SDL_RenderPresent(handle); }
    };

    class RenderTarget {
    public:
        SDL_Texture* handle;
        int32_t texel_width, texel_height;
        int32_t logical_width, logical_height;
        float density;

        void Destroy() {
            if (handle) SDL_DestroyTexture(handle);
            handle = nullptr;
        }

        void Ensure(Renderer* renderer, int32_t width, int32_t height, float pixel_density, int32_t supersample) {
            if (width <= 0 || height <= 0) return;

            if (handle && width == logical_width && height == logical_height && pixel_density == density) return;

            Destroy();

            logical_width = width;
            logical_height = height;
            density = pixel_density;

            texel_width  = SDL_max(1, (int32_t)(width  * pixel_density * supersample));
            texel_height = SDL_max(1, (int32_t)(height * pixel_density * supersample));

            handle = SDL_CreateTexture(renderer->handle, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, texel_width, texel_height);
            if (handle) {
                SDL_SetTextureScaleMode(handle, SDL_SCALEMODE_LINEAR);
                SDL_SetTextureBlendMode(handle, SDL_BLENDMODE_BLEND);
            }
        }

        void Ensure(Renderer* renderer, Window* window, int32_t supersample) {
            int32_t width, height;
            window->GetSize(&width, &height);
            Ensure(renderer, width, height, window->PixelDensity(), supersample);
        }
    };
}

// base_arena.hpp
#pragma once

#if defined(__APPLE__) || defined(__MACH__)
#error "Apple platforms are explicitly unsupported."
#endif

#include <SDL3/SDL.h>
#include <cstdint>
#include <new>

#ifndef ARENA_FREE_LIST
#define ARENA_FREE_LIST 1
#endif

constexpr float TurnFull    = 1.0f;
constexpr float TurnHalf    = 0.5f;
constexpr float TurnQuarter = 0.25f;
constexpr double RadiansPerTurn = 6.28318530717958647692;

constexpr float VEC2_NORMALIZE_TOLERANCE = 0.0001f;
constexpr uint64_t ARENA_MAX_ALLOCATION = UINT64_C(0x8000000000000000);

constexpr float TurnsToRadians(float turns) { return (float)(turns * RadiansPerTurn); }
constexpr float RadiansToTurns(float radians) { return (float)(radians / RadiansPerTurn); }

consteval uint64_t KB(uint64_t x) { return x << 10; }
consteval uint64_t MB(uint64_t x) { return x << 20; }
consteval uint64_t GB(uint64_t x) { return x << 30; }

constexpr uint64_t NextPow2(uint64_t v) noexcept {
    if (v <= 1) return 1;
    v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v |= v >> 32;
    return v + 1;
}

constexpr uint64_t ALIGN_POW2(uint64_t x, uint64_t p) {
    if (p <= 1) 
        return x;
    p = NextPow2(p);
    return (x + p - 1) & ~(p - 1);
}

constexpr uint64_t ARENA_DEFAULT_RESERVE = MB(256);
constexpr uint64_t ARENA_DEFAULT_COMMIT  = KB(64);
constexpr uint64_t ARENA_HEADER_SIZE     = 128;

enum ArenaFlags : uint64_t {
    ARENA_FLAG_NONE               = 0,
    ARENA_FLAG_NO_CHAIN           = (1 << 0),
    ARENA_FLAG_IS_BACKING_BUFFER  = (1 << 16)
};

#if defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define ARENA_ASAN_ENABLED 1
    #endif
#endif
#if defined(__SANITIZE_ADDRESS__)
    #define ARENA_ASAN_ENABLED 1
#endif

#if defined(ARENA_ASAN_ENABLED)
    extern "C" {
        void __asan_poison_memory_region(void const volatile *addr, size_t size);
        void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
    }
    #define AsanPoisonMemoryRegion(ptr, size)   __asan_poison_memory_region((ptr), (size))
    #define AsanUnpoisonMemoryRegion(ptr, size) __asan_unpoison_memory_region((ptr), (size))
#else
    #define AsanPoisonMemoryRegion(ptr, size)   ((void)0)
    #define AsanUnpoisonMemoryRegion(ptr, size) ((void)0)
#endif

class ArenaParams {
public:
    uint64_t reserve_size{ARENA_DEFAULT_RESERVE};
    uint64_t commit_size{ARENA_DEFAULT_COMMIT};
    uint64_t flags{ARENA_FLAG_NONE};
    void* optional_backing_buffer{nullptr};
    const char* allocation_site_file{nullptr};
    int32_t allocation_site_line{0};
    const char* name{nullptr};
};

class Arena {
public:
    Arena* prev{nullptr};
    Arena* current{nullptr};

    uint64_t flags{ARENA_FLAG_NONE};
    uint64_t commit_step{0};
    uint64_t reserve_step{0};

    uint64_t base_pos{0};
    uint64_t current_pos{0};
    uint64_t commit_pos{0};
    uint64_t reserve_pos{0};

    const char* allocation_site_file{nullptr};
    int32_t allocation_site_line{0};
    const char* name{nullptr};

#if ARENA_FREE_LIST
    Arena* free_last{nullptr};
#endif
    [[nodiscard]] void* Push(uint64_t size, uint64_t align = 8, bool zero = true);
    void  PopTo(uint64_t target_pos);
    void  Pop(uint64_t amount);
    void  Clear();
    void  Release();
    [[nodiscard]] uint64_t Pos() const;
};

static_assert(sizeof(Arena) <= ARENA_HEADER_SIZE, "Arena header exceeds predefined layout boundary size.");

[[nodiscard]] Arena* ArenaAlloc(const ArenaParams& params = {});

template <typename T>
[[nodiscard]] T* PushArray(Arena* arena, uint64_t count, uint64_t align = alignof(T), bool zero = true) {
    if (!arena) 
        return nullptr;
    if (count > 0 && sizeof(T) > UINT64_MAX / count) 
        return nullptr;
    return static_cast<T*>(arena->Push(sizeof(T) * count, align, zero));
}

template <typename T>
[[nodiscard]] T* PushStruct(Arena* arena, uint64_t align = alignof(T), bool zero = true) {
    return PushArray<T>(arena, 1, align, zero);
}

template <typename T>
[[nodiscard]] T* PushObject(Arena* arena) {
    void* memory = arena->Push(sizeof(T), alignof(T), true);
    if (!memory) return nullptr;
    return new (memory) T();
}

class Vec2 {
public:
    float X{0.0f};
    float Y{0.0f};

    Vec2() = default;
    Vec2(float x, float y) : X(x), Y(y) {}

    inline float& operator[](int32_t index) { return (&X)[index]; }
    inline const float& operator[](int32_t index) const { return (&X)[index]; }

    inline Vec2 Add(Vec2 b) const { return Vec2(X + b.X, Y + b.Y); }
    inline Vec2 Sub(Vec2 b) const { return Vec2(X - b.X, Y - b.Y); }
    inline Vec2 Scale(float s) const { return Vec2(X * s, Y * s); }
    inline float Length() const { return SDL_sqrtf(X * X + Y * Y); }
                    
    inline Vec2 Normalize() const {
        float len = Length();
        if (len < VEC2_NORMALIZE_TOLERANCE)
            return Vec2(0.0f, 0.0f);
        return Scale(1.0f / len);
    }

    inline Vec2 operator+(Vec2 b) const { return Add(b); }
    inline Vec2 operator-(Vec2 b) const { return Sub(b); }
    inline Vec2 operator*(float s)  const { return Scale(s); }
    inline Vec2& operator+=(Vec2 b) { X += b.X; Y += b.Y; return *this; }
    inline Vec2& operator-=(Vec2 b) { X -= b.X; Y -= b.Y; return *this; }
};

float Distance2D(Vec2 a, Vec2 b);

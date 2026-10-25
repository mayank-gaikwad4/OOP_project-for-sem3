// base_arena.cpp
#include "base_arena.hpp"

#if defined(linux) || defined(__linux) || defined(__linux__)
    #include <sys/mman.h>
    #include <unistd.h>
    inline void OS_Init() {}
    inline void* OS_Reserve(uint64_t size) {
        void* p = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return nullptr;
        return p;
    }
    inline bool OS_Commit(void* ptr, uint64_t size) { return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0; }
    inline void OS_Decommit(void* ptr, uint64_t size) { madvise(ptr, size, MADV_DONTNEED); mprotect(ptr, size, PROT_NONE); }
    inline void OS_Release(void* ptr, uint64_t size) { munmap(ptr, size); }
    inline uint64_t OS_GetPageSize() { return static_cast<uint64_t>(getpagesize()); }
#elif defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    inline void OS_Init() {}
    inline void* OS_Reserve(uint64_t size) { return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS); }
    inline bool OS_Commit(void* ptr, uint64_t size) { return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr; }
    inline void OS_Decommit(void* ptr, uint64_t size) { VirtualFree(ptr, size, MEM_DECOMMIT); }
    inline void OS_Release(void* ptr, uint64_t size) { (void)size; VirtualFree(ptr, 0, MEM_RELEASE); }
    inline uint64_t OS_GetPageSize() { SYSTEM_INFO si; GetSystemInfo(&si); return static_cast<uint64_t>(si.dwPageSize); }
    inline uint64_t OS_GetAllocationGranularity() { SYSTEM_INFO si; GetSystemInfo(&si); return static_cast<uint64_t>(si.dwAllocationGranularity); }
#endif

bool g_os_layer_initialized = false;

Arena* ArenaAlloc(const ArenaParams& params) {
    if (!g_os_layer_initialized) {
        OS_Init();
        g_os_layer_initialized = true;
    }

    uint64_t page_size = OS_GetPageSize();
    uint64_t reserve_size = ARENA_DEFAULT_RESERVE;
    if (params.reserve_size) reserve_size = params.reserve_size;
    uint64_t commit_size = ARENA_DEFAULT_COMMIT;
    if (params.commit_size) commit_size = params.commit_size;
    uint64_t flags        = params.flags;

    if (reserve_size > ARENA_MAX_ALLOCATION || commit_size > SDL_UINT64_C(0x8000000000000000)) 
        return nullptr;

    void* base = params.optional_backing_buffer;

    if (base == nullptr) {
        uint64_t next_res = NextPow2(reserve_size);
        uint64_t next_cmt = NextPow2(commit_size);
        if (next_res < reserve_size || next_cmt < commit_size) return nullptr;

#if defined(_WIN32)
        uint64_t alloc_granularity = OS_GetAllocationGranularity();
        reserve_size = ALIGN_POW2(next_res, alloc_granularity);
#else
        reserve_size = ALIGN_POW2(next_res, page_size);
#endif
        commit_size  = ALIGN_POW2(next_cmt, page_size);
        if (commit_size > reserve_size) reserve_size = commit_size;

        base = OS_Reserve(reserve_size);
        if (base != nullptr) {
            if (!OS_Commit(base, commit_size)) {
                OS_Release(base, reserve_size);
                return nullptr;
            }
        }
        if (base == nullptr) 
            return nullptr;
    } else {
        flags |= ARENA_FLAG_IS_BACKING_BUFFER;
        flags |= ARENA_FLAG_NO_CHAIN;
        commit_size = reserve_size;
    }

    AsanPoisonMemoryRegion(base, reserve_size);
    AsanUnpoisonMemoryRegion(base, ARENA_HEADER_SIZE);

    Arena* arena = static_cast<Arena*>(base);
    SDL_memset(arena, 0, sizeof(Arena)); 

    arena->prev         = nullptr;
    arena->current      = arena;
    arena->flags        = flags;
    arena->commit_step  = commit_size;
    arena->reserve_step = reserve_size;
    arena->base_pos     = 0;
    arena->current_pos  = ARENA_HEADER_SIZE;
    arena->commit_pos   = commit_size;
    arena->reserve_pos  = reserve_size;

    arena->allocation_site_file = params.allocation_site_file;
    arena->allocation_site_line = params.allocation_site_line;
    arena->name                 = params.name;

#if ARENA_FREE_LIST
    arena->free_last  = nullptr;
#endif

    return arena;
}

void* Arena::Push(uint64_t size, uint64_t align, bool zero) {
    if (align == 0) 
        align = 1;

    Arena* curr = current;
    if (!curr) 
        return nullptr;

    uint64_t aligned_pos = ALIGN_POW2(curr->current_pos, align);
    uint64_t next_pos = aligned_pos + size;

    if (next_pos < aligned_pos || next_pos < size) 
        return nullptr;

    if (next_pos > curr->reserve_pos) {
        if (curr->flags & ARENA_FLAG_NO_CHAIN) 
            return nullptr;
        Arena* next_chunk = nullptr;

#if ARENA_FREE_LIST
        Arena* prev_block = nullptr;
        for (Arena* block = free_last; block != nullptr; prev_block = block, block = block->prev) {
            uint64_t block_aligned_pos = ALIGN_POW2(block->current_pos, align);
            if (block->reserve_pos >= block_aligned_pos + size) {
                if (prev_block != nullptr) 
                    prev_block->prev = block->prev;
                else free_last = block->prev;
                next_chunk = block;
                break;
            }
        }
#endif

        if (next_chunk == nullptr) {
            uint64_t needed_size = ALIGN_POW2(ARENA_HEADER_SIZE, align) + size;
            uint64_t new_reserve = ALIGN_POW2(needed_size, curr->reserve_step);
            uint64_t new_commit  = ALIGN_POW2(needed_size, curr->commit_step);

            ArenaParams next_params{
                .reserve_size = new_reserve,
                .commit_size  = new_commit,
                .flags = flags,
                .name = name
            };

            next_chunk = ArenaAlloc(next_params);
            if (next_chunk == nullptr) 
                return nullptr;
        }

        next_chunk->base_pos = curr->base_pos + curr->reserve_pos;
        next_chunk->prev     = curr;
        current              = next_chunk;
        curr                 = next_chunk;

        aligned_pos = ALIGN_POW2(curr->current_pos, align);
        next_pos    = aligned_pos + size;
    }

    if (next_pos > curr->commit_pos) {
        if (curr->flags & ARENA_FLAG_IS_BACKING_BUFFER) 
            return nullptr;

        uint64_t page_size = OS_GetPageSize();
        uint64_t needed_commit = ALIGN_POW2(next_pos, curr->commit_step);
        needed_commit = ALIGN_POW2(needed_commit, page_size);

        if (needed_commit > curr->reserve_pos) 
            needed_commit = curr->reserve_pos;

        uint64_t commit_req = needed_commit - curr->commit_pos;
        bool commit_ok = OS_Commit(reinterpret_cast<uint8_t*>(curr) + curr->commit_pos, commit_req);
        if (!commit_ok) 
            return nullptr;

        curr->commit_pos = needed_commit;
    }
    void* result = reinterpret_cast<uint8_t*>(curr) + aligned_pos;
    AsanUnpoisonMemoryRegion(result, size);
    if (zero) 
        SDL_memset(result, 0, size);

    curr->current_pos = next_pos;

    if (curr->commit_pos > curr->current_pos) {
        AsanPoisonMemoryRegion(reinterpret_cast<uint8_t*>(curr) + curr->current_pos, curr->commit_pos - curr->current_pos);
    }

    return result;
}

uint64_t Arena::Pos() const {
    if (!current) 
        return 0;
    return current->base_pos + current->current_pos;
}

void Arena::PopTo(uint64_t target_pos) {
    if (target_pos > Pos()) 
        return;

    Arena* curr = current;

    while (curr != nullptr && target_pos < curr->base_pos) {
        Arena* prev_block = curr->prev;
#if ARENA_FREE_LIST
        curr->current_pos = ARENA_HEADER_SIZE;
        curr->prev = free_last;
        free_last = curr;
        AsanPoisonMemoryRegion(reinterpret_cast<uint8_t*>(curr) + ARENA_HEADER_SIZE, curr->reserve_pos - ARENA_HEADER_SIZE);
#else
        AsanUnpoisonMemoryRegion(curr, curr->reserve_pos);
        if (!(curr->flags & ARENA_FLAG_IS_BACKING_BUFFER)) OS_Release(curr, curr->reserve_pos);
#endif
        curr = prev_block;
    }

    if (curr != nullptr) {
        current = curr;
        uint64_t local_pos = target_pos - curr->base_pos;
        if (local_pos < ARENA_HEADER_SIZE) 
            local_pos = ARENA_HEADER_SIZE;

        if (local_pos < curr->current_pos) {
            AsanPoisonMemoryRegion(reinterpret_cast<uint8_t*>(curr) + local_pos, curr->current_pos - local_pos);
        }
        curr->current_pos = local_pos;
    }
}

void Arena::Pop(uint64_t amount) {
    uint64_t total_pos = Pos();
    if (amount <= total_pos) 
        PopTo(total_pos - amount);
}

void Arena::Clear() {
    PopTo(ARENA_HEADER_SIZE);
}

void Arena::Release() {
#if ARENA_FREE_LIST
    for (Arena* free_block = free_last; free_block != nullptr;) {
        Arena* prev_block = free_block->prev;
        bool is_backing = (free_block->flags & ARENA_FLAG_IS_BACKING_BUFFER);
        uint64_t block_res  = free_block->reserve_pos;

        AsanUnpoisonMemoryRegion(free_block, block_res);
        if (!is_backing) 
            OS_Release(free_block, block_res);

        free_block = prev_block;
    }
    free_last = nullptr;
#endif

    Arena* active_block = current;
    current = nullptr;
    while (active_block != nullptr) {
        Arena* prev_block = active_block->prev;
        bool is_backing = (active_block->flags & ARENA_FLAG_IS_BACKING_BUFFER);
        uint64_t block_res  = active_block->reserve_pos;

        AsanUnpoisonMemoryRegion(active_block, block_res);
        if (!is_backing) 
            OS_Release(active_block, block_res);

        active_block = prev_block;
    }
}

float Distance2D(Vec2 a, Vec2 b) { return a.Sub(b).Length(); }

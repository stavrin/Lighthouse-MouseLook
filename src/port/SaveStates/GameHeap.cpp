// [port] The game's heap (bk_malloc / bk_free / bk_realloc) in one fixed block of memory.
//
// Lighthouse first sent these straight to the C runtime's calloc/free, which scatters the game's
// objects among everything else the program allocates. Save states need the game's memory in one
// place they can copy and put back, so they come from this arena instead, managed by TLSF (a
// small, fast allocator that keeps its own bookkeeping inside the arena, so copying the arena
// copies the allocator's state too). If the arena ever runs out, allocations fall back to calloc
// (save states are then refused; see port_gameHeap_overflowed).

#include <cstdlib>
#include <cstring>
#include <mutex>

#include <spdlog/spdlog.h>

#include "port/SaveStates/GameHeap.h"
#include "port/SaveStates/tlsf/tlsf.h"

namespace {
// The game used about 2 MB on the N64; 64-bit pointers and the port's own additions take more.
constexpr size_t kArenaSize = 96u * 1024u * 1024u;

alignas(64) unsigned char sArena[kArenaSize];
tlsf_t sTlsf = nullptr;
std::mutex sHeapMutex;
bool sOverflowed = false;

void EnsureInit() {
    if (sTlsf == nullptr) {
        sTlsf = tlsf_create_with_pool(sArena, kArenaSize);
    }
}

bool InArena(const void* ptr) {
    const unsigned char* p = static_cast<const unsigned char*>(ptr);
    return p >= sArena && p < sArena + kArenaSize;
}

void Walker(void* ptr, size_t size, int used, void* user) {
    if (used) {
        unsigned char* end = static_cast<unsigned char*>(ptr) + size;
        unsigned char** furthest = static_cast<unsigned char**>(user);
        if (end > *furthest) {
            *furthest = end;
        }
    }
}
} // namespace

extern "C" void* port_gameHeap_calloc(size_t size) {
    std::lock_guard<std::mutex> lock(sHeapMutex);
    EnsureInit();
    void* ptr = tlsf_malloc(sTlsf, size > 0 ? size : 1);
    if (ptr == nullptr) {
        if (!sOverflowed) {
            SPDLOG_WARN("[GameHeap] arena full ({} MB): falling back to calloc; save states disabled",
                        kArenaSize / (1024 * 1024));
        }
        sOverflowed = true;
        return calloc(1, size);
    }
    memset(ptr, 0, size);
    return ptr;
}

extern "C" void port_gameHeap_free(void* ptr) {
    if (ptr == nullptr) {
        return;
    }
    if (!InArena(ptr)) {
        free(ptr);
        return;
    }
    std::lock_guard<std::mutex> lock(sHeapMutex);
    tlsf_free(sTlsf, ptr);
}

extern "C" void* port_gameHeap_realloc(void* ptr, size_t size) {
    if (ptr == nullptr) {
        return port_gameHeap_calloc(size);
    }
    if (!InArena(ptr)) {
        return realloc(ptr, size);
    }
    std::lock_guard<std::mutex> lock(sHeapMutex);
    void* moved = tlsf_realloc(sTlsf, ptr, size);
    if (moved == nullptr && size > 0) {
        // Arena full: move it out to the C runtime's heap.
        void* out = malloc(size);
        if (out != nullptr) {
            size_t old = tlsf_block_size(ptr);
            memcpy(out, ptr, old < size ? old : size);
            tlsf_free(sTlsf, ptr);
        }
        sOverflowed = true;
        return out;
    }
    return moved;
}

extern "C" int port_gameHeap_overflowed(void) {
    return sOverflowed ? 1 : 0;
}

extern "C" void port_gameHeap_lock(void) {
    sHeapMutex.lock();
}

extern "C" void port_gameHeap_unlock(void) {
    sHeapMutex.unlock();
}

extern "C" unsigned char* port_gameHeap_base(void) {
    return sArena;
}

extern "C" size_t port_gameHeap_size(void) {
    return kArenaSize;
}

// How much of the arena, from its start, holds anything in use (and the allocator's own records).
// Call with the heap locked.
extern "C" size_t port_gameHeap_usedExtent(void) {
    if (sTlsf == nullptr) {
        return 0;
    }
    unsigned char* furthest = sArena;
    tlsf_walk_pool(tlsf_get_pool(sTlsf), Walker, &furthest);
    // The free block after the last used one keeps its size and links in a header just past it.
    size_t extent = static_cast<size_t>(furthest - sArena) + 256;
    extent = (extent + 63) & ~static_cast<size_t>(63);
    return extent < kArenaSize ? extent : kArenaSize;
}

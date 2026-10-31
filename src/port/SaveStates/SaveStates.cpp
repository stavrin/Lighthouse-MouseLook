// [port] Save states: save the whole game at any moment and go back to it (F5 save, F7 load,
// F6 next slot), like an emulator. Experimental; turn on in Enhancements > Save States.
//
// The idea is Ship of Harkinian's (soh/Enhancements/savestates.cpp): copy the game's memory at a
// safe point between frames and copy it back later. SoH can copy OoT's fixed heaps and list the
// few static variables that live outside them. Banjo keeps much more in file-level variables, so
// instead of listing them, the build groups every game variable (all of src/ except src/port, and
// libultra's audio library) into two sections of their own (.gsd for initialised data, .gsb for
// the rest; SaveStateSections.h, applied to those files by CMakeLists.txt), and the game's heap
// lives in one arena (GameHeap.cpp). A state is a copy of those two sections and the used part of
// the arena.
//
// Some game variables are live operating-system objects shared with other threads (threads,
// message queues, timers, in-flight RCP tasks): those are put back as they are now after a load,
// not as they were (each file lists its own in a port_saveStateExclusions_* function).
//
// States live in memory only: they're gone when the game closes (addresses change between runs).
// Loading a state from another area keeps working because map models the game reloads are kept
// alive while any state exists (ResourceHelpers.cpp).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>

#include "port/SaveStates/GameHeap.h"
#include "port/SaveStates/SaveStates.h"
#include "port/UI/Notification.h"
#include "port/UI/cvar_prefixes.h"

extern "C" {
#include "port/OS/OS.h"
void port_lockAudio(void);
void port_unlockAudio(void);
void FrameInterpolation_ShouldInterpolateFrame(bool shouldInterpolate);
void FrameInterpolation_DontInterpolateCamera(void);

// Markers around the grouped sections (SaveStateMarkers.c).
extern char gSaveStateDataBegin, gSaveStateDataEnd, gSaveStateBssBegin, gSaveStateBssEnd;

// Known game variables, to check the grouping really happened in this build.
extern unsigned char D_8037C060;           // ba_camera.c: uninitialised
extern unsigned int heap_occupiedBytes;    // memory.c: = 0
extern int D_802758D0;                     // audio_soundplayer.c: = 1

// Each file's live OS objects.
typedef void (*SaveStateExcludeFn)(void* ptr, size_t size);
void port_saveStateExclusions_init(SaveStateExcludeFn exclude);
void port_saveStateExclusions_initthread(SaveStateExcludeFn exclude);
void port_saveStateExclusions_joy(SaveStateExcludeFn exclude);
void port_saveStateExclusions_vimgr(SaveStateExcludeFn exclude);
void port_saveStateExclusions_graphicsThread(SaveStateExcludeFn exclude);
void port_saveStateExclusions_defragmanager(SaveStateExcludeFn exclude);
void port_saveStateExclusions_audioManager(SaveStateExcludeFn exclude);
void port_saveStateExclusions_displayList(SaveStateExcludeFn exclude);
void port_saveStateExclusions_bamotor(SaveStateExcludeFn exclude);
}

#define CVAR_SAVESTATES CVAR_ENHANCEMENT("SaveStates.Enabled")

namespace {
constexpr int kSlots = 5;

struct Range {
    unsigned char* ptr;
    size_t size;
};

struct Slot {
    bool used = false;
    std::vector<unsigned char> data;
    std::vector<unsigned char> bss;
    std::vector<unsigned char> heap;
    std::vector<unsigned char> heapTail; // the allocator's end-of-arena record
};

Slot sSlots[kSlots];
int sCurrentSlot = 0;
std::atomic<int> sRequest{ 0 }; // 0 none, 1 save, 2 load
bool sChecked = false;
bool sAvailable = false;
std::vector<Range> sExcluded;

constexpr size_t kHeapTail = 256;

Range DataRange() {
    return { reinterpret_cast<unsigned char*>(&gSaveStateDataBegin),
             static_cast<size_t>(&gSaveStateDataEnd - &gSaveStateDataBegin) };
}

Range BssRange() {
    return { reinterpret_cast<unsigned char*>(&gSaveStateBssBegin),
             static_cast<size_t>(&gSaveStateBssEnd - &gSaveStateBssBegin) };
}

bool Inside(const void* p, Range r) {
    const unsigned char* c = static_cast<const unsigned char*>(p);
    return c >= r.ptr && c < r.ptr + r.size;
}

bool InSections(const void* p) {
    return Inside(p, DataRange()) || Inside(p, BssRange());
}

void Exclude(void* ptr, size_t size) {
    sExcluded.push_back({ static_cast<unsigned char*>(ptr), size });
}

// Once: check the build grouped the game's variables, and collect the live OS objects.
bool Available() {
    if (sChecked) {
        return sAvailable;
    }
    sChecked = true;
#ifdef _MSC_VER
    const Range data = DataRange(), bss = BssRange();
    const bool ordered = data.size > 0 && data.size < (512u << 20) && bss.size > 0 && bss.size < (512u << 20);
    const bool grouped = ordered && InSections(&D_8037C060) && InSections(&heap_occupiedBytes) &&
                         InSections(&D_802758D0);
    SPDLOG_INFO("[SaveStates] game data {} KB, bss {} KB, grouped: {}", data.size / 1024, bss.size / 1024,
                grouped ? "yes" : "NO");
    if (!grouped) {
        return sAvailable = false;
    }
    port_saveStateExclusions_init(Exclude);
    port_saveStateExclusions_initthread(Exclude);
    port_saveStateExclusions_joy(Exclude);
    port_saveStateExclusions_vimgr(Exclude);
    port_saveStateExclusions_graphicsThread(Exclude);
    port_saveStateExclusions_defragmanager(Exclude);
    port_saveStateExclusions_audioManager(Exclude);
    port_saveStateExclusions_displayList(Exclude);
    port_saveStateExclusions_bamotor(Exclude);
    for (const Range& r : sExcluded) {
        if (!InSections(r.ptr)) {
            SPDLOG_WARN("[SaveStates] an excluded object isn't in the grouped sections");
        }
    }
    return sAvailable = true;
#else
    return sAvailable = false; // the section grouping is only set up for MSVC builds so far
#endif
}

void Notify(const std::string& message, bool problem = false) {
    Notification::Emit({ .prefix = "Save State",
                         .message = message,
                         .messageColor = problem ? ImVec4(1.0f, 0.6f, 0.5f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f) });
}

// The renderer reads display lists out of game memory: let it finish the frame it's drawing.
void WaitForRcp() {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (OS_SpPeekPendingTask() != nullptr && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Copy(std::vector<unsigned char>& to, Range from) {
    to.assign(from.ptr, from.ptr + from.size);
}

void Save(int slot) {
    if (port_gameHeap_overflowed()) {
        Notify("the game ran out of save-state memory; can't save", true);
        return;
    }
    Slot& s = sSlots[slot];
    WaitForRcp();
    port_lockAudio();
    port_gameHeap_lock();
    Copy(s.data, DataRange());
    Copy(s.bss, BssRange());
    unsigned char* heap = port_gameHeap_base();
    const size_t extent = port_gameHeap_usedExtent();
    s.heap.assign(heap, heap + extent);
    s.heapTail.assign(heap + port_gameHeap_size() - kHeapTail, heap + port_gameHeap_size());
    port_gameHeap_unlock();
    port_unlockAudio();
    s.used = true;
    SPDLOG_INFO("[SaveStates] saved slot {} ({} KB)", slot + 1, (s.data.size() + s.bss.size() + s.heap.size()) / 1024);
    Notify("saved to slot " + std::to_string(slot + 1));
}

void Load(int slot) {
    Slot& s = sSlots[slot];
    if (!s.used) {
        Notify("slot " + std::to_string(slot + 1) + " is empty", true);
        return;
    }
    const Range data = DataRange(), bss = BssRange();
    if (s.data.size() != data.size || s.bss.size() != bss.size) {
        Notify("that state doesn't fit this build", true);
        return;
    }
    WaitForRcp();
    port_lockAudio();
    port_gameHeap_lock();
    // Live OS objects stay as they are now.
    std::vector<std::vector<unsigned char>> keep;
    keep.reserve(sExcluded.size());
    for (const Range& r : sExcluded) {
        keep.emplace_back(r.ptr, r.ptr + r.size);
    }
    memcpy(data.ptr, s.data.data(), data.size);
    memcpy(bss.ptr, s.bss.data(), bss.size);
    unsigned char* heap = port_gameHeap_base();
    memcpy(heap, s.heap.data(), s.heap.size());
    memcpy(heap + port_gameHeap_size() - kHeapTail, s.heapTail.data(), kHeapTail);
    for (size_t i = 0; i < sExcluded.size(); i++) {
        memcpy(sExcluded[i].ptr, keep[i].data(), sExcluded[i].size);
    }
    port_gameHeap_unlock();
    port_unlockAudio();
    // Don't blend the frame before the load into the one after it.
    FrameInterpolation_ShouldInterpolateFrame(false);
    FrameInterpolation_DontInterpolateCamera();
    SPDLOG_INFO("[SaveStates] loaded slot {}", slot + 1);
    Notify("loaded slot " + std::to_string(slot + 1));
}
} // namespace

// From the keyboard (Engine.cpp): F5 save, F6 next slot, F7 load. The save or load itself waits
// for the game thread to reach the end of its frame (port_saveStates_process).
extern "C" void port_saveStates_key(int key) {
    if (CVarGetInteger(CVAR_SAVESTATES, 0) == 0) {
        return;
    }
    switch (key) {
        case PORT_SAVESTATE_KEY_SAVE:
            sRequest = 1;
            break;
        case PORT_SAVESTATE_KEY_LOAD:
            sRequest = 2;
            break;
        case PORT_SAVESTATE_KEY_NEXT_SLOT:
            sCurrentSlot = (sCurrentSlot + 1) % kSlots;
            Notify("slot " + std::to_string(sCurrentSlot + 1) + (sSlots[sCurrentSlot].used ? "" : " (empty)"));
            break;
    }
}

// The game thread, at the end of a game frame while a game is loaded (init.c, mainLoop).
extern "C" void port_saveStates_process(void) {
    const int request = sRequest.exchange(0);
    if (request == 0) {
        return;
    }
    if (!Available()) {
        Notify("not available in this build (see the log)", true);
        return;
    }
    if (request == 1) {
        Save(sCurrentSlot);
    } else {
        Load(sCurrentSlot);
    }
}

extern "C" int port_saveStates_any(void) {
    for (const Slot& s : sSlots) {
        if (s.used) {
            return 1;
        }
    }
    return 0;
}

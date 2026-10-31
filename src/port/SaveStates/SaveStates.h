#pragma once

#include <stddef.h>

#define PORT_SAVESTATE_KEY_SAVE 1
#define PORT_SAVESTATE_KEY_NEXT_SLOT 2
#define PORT_SAVESTATE_KEY_LOAD 3

#ifdef __cplusplus
extern "C" {
#endif

// Save states (SaveStates.cpp).
void port_saveStates_key(int key);
void port_saveStates_process(void);
int port_saveStates_any(void);

// Each game file with live OS objects lists them for save states with one of these.
typedef void (*SaveStateExcludeFn)(void* ptr, size_t size);

#ifdef __cplusplus
}
#endif

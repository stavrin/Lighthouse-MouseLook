#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The game's heap: one fixed arena, so save states can copy it (GameHeap.cpp).
void* port_gameHeap_calloc(size_t size);
void port_gameHeap_free(void* ptr);
void* port_gameHeap_realloc(void* ptr, size_t size);
int port_gameHeap_overflowed(void);
void port_gameHeap_lock(void);
void port_gameHeap_unlock(void);
unsigned char* port_gameHeap_base(void);
size_t port_gameHeap_size(void);
size_t port_gameHeap_usedExtent(void);

#ifdef __cplusplus
}
#endif

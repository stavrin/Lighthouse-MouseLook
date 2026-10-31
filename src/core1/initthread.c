// BanjoDecomp: initthread.c
#include <ultra64.h>
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"

#define INIT_THREAD_STACK_SIZE 512

u8 sInitThreadStack[INIT_THREAD_STACK_SIZE]; // Size based on the previous symbol's address
OSThread sInitThread;

void initThread_create(void) {
    osCreateThread(&sInitThread, INITTHREAD_ID, initThread_entry, NULL, sInitThreadStack + INIT_THREAD_STACK_SIZE, OS_PRIORITY_IDLE);
    osStartThread(&sInitThread);
}

void initThread_entry(void *arg) {
    parallel_init();
    mainThread_create();
    osStartThread(mainThread_get());
    while (1);
}

#include <stddef.h>

// [port] Save states: this file's live OS objects (threads, queues, timers, in-flight tasks).
// A loaded state leaves them as they are (port/SaveStates/SaveStates.cpp).
void port_saveStateExclusions_initthread(void (*exclude)(void *ptr, size_t size)) {
    exclude((void *)&sInitThread, sizeof(sInitThread));
    exclude((void *)&sInitThreadStack, sizeof(sInitThreadStack));
}

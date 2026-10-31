// BanjoDecomp: defragmanager.c
#include <ultra64.h>
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"

#define DEFRAG_THREAD_STACK_SIZE 2048

OSMesgQueue sDefragThreadResumeSyncQueue;
OSMesg      sDefragThreadResumeSyncMesg;
OSMesgQueue sDefragThreadPauseSyncQueue;
OSMesg      sDefragThreadPauseSyncMesg;
OSThread    sDefragThread;
u8          sDefragThreadStack[DEFRAG_THREAD_STACK_SIZE];

/* .code */
void defragthread_init(void){
    osCreateMesgQueue(&sDefragThreadResumeSyncQueue, &sDefragThreadResumeSyncMesg, 1);
    osCreateMesgQueue(&sDefragThreadPauseSyncQueue, &sDefragThreadPauseSyncMesg, 1);
    osCreateThread(&sDefragThread, DEFRAGMANAGER_THREAD_ID, defragthread_entry, NULL, sDefragThreadStack + DEFRAG_THREAD_STACK_SIZE, DEFRAGMANAGER_THREAD_PRI);
    osStartThread(&sDefragThread);
}

void defragthread_free(void){
    osStopThread(&sDefragThread);
    osDestroyThread(&sDefragThread);
}

void defragthread_resume(void){
    if(func_8023E000() == 3){
        osSendMesgPtr(&sDefragThreadResumeSyncQueue, NULL, OS_MESG_BLOCK);
    }
}

void defragthread_pause(void){
    if(func_8023E000() == 3){
        osSendMesgPtr(&sDefragThreadPauseSyncQueue, NULL, OS_MESG_BLOCK);
    }
}

void defragthread_setPriority(OSPri pri){
    if(func_8023E000() == 3){
        osSetThreadPri(&sDefragThread, pri);
    }
}

void defragthread_entry(void *arg) {
    int tmp_v0;
    do{
        osRecvMesg(&sDefragThreadResumeSyncQueue, NULL, OS_MESG_BLOCK);
        if(!sDefragThreadPauseSyncQueue.validCount){
            do{
                tmp_v0 = game_defrag();
            }while(!sDefragThreadPauseSyncQueue.validCount && tmp_v0);
        }
        osRecvMesg(&sDefragThreadPauseSyncQueue, NULL, OS_MESG_BLOCK);
    }while(1);
}

#include <stddef.h>

// [port] Save states: this file's live OS objects (threads, queues, timers, in-flight tasks).
// A loaded state leaves them as they are (port/SaveStates/SaveStates.cpp).
void port_saveStateExclusions_defragmanager(void (*exclude)(void *ptr, size_t size)) {
    exclude((void *)&sDefragThreadResumeSyncQueue, sizeof(sDefragThreadResumeSyncQueue));
    exclude((void *)&sDefragThreadResumeSyncMesg, sizeof(sDefragThreadResumeSyncMesg));
    exclude((void *)&sDefragThreadPauseSyncQueue, sizeof(sDefragThreadPauseSyncQueue));
    exclude((void *)&sDefragThreadPauseSyncMesg, sizeof(sDefragThreadPauseSyncMesg));
    exclude((void *)&sDefragThread, sizeof(sDefragThread));
    exclude((void *)&sDefragThreadStack, sizeof(sDefragThreadStack));
}

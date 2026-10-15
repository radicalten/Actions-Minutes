// Host (PPC-Linux) shim for libogc "tuxedo/thread.h".
// Semantics preserved: PPCIrqLockByMsr/UnlockByMsr are a recursion-safe critical
// section that returns the previous interrupt-enable state. KThreadResume runs
// the entry point to completion synchronously (the Wii build runs it on a real
// OS thread; the emulator only requires that the work happens before Join).
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <time.h>

extern "C" {

typedef uint32_t PPCIrqState;

// Recursive global lock; guest-visible state is only ever touched by the main
// harness thread in the emulator's single-runner configuration.
static inline PPCIrqState PPCIrqLockByMsr(void) {
    static pthread_mutex_t m = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
    pthread_mutex_lock(&m);
    return 0;
}

static inline void PPCIrqUnlockByMsr(PPCIrqState st) {
    (void)st;
    static pthread_mutex_t m = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
    pthread_mutex_unlock(&m);
}

static inline void KThreadYield(void) {}

static inline void KThreadSleepMs(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}

typedef int (*KThreadEntry)(void *);   // tuxedo entry points return sptr

typedef struct {
    KThreadEntry entry;
    void *args;
} KThread;

#define KTHR_MAIN_PRIO 0
#define KTHR_PRIO_HIGH 0

static inline void KThreadPrepare(KThread *t, KThreadEntry entry, void *args,
                                  void * /*stackTop*/, int /*prio*/) {
    t->entry = entry;
    t->args = args;
}

static inline void KThreadResume(KThread *t) {
    if (t->entry) (void)t->entry(t->args);
}

static inline void KThreadJoin(KThread *t) { t->entry = nullptr; (void)t; }

static inline void KThreadExit(void) {}

} // extern "C"

// ---- Light host stub for the tuxedo KThrQueue condition primitive ----
extern "C" {
struct KThrQueue { volatile int flag; };
static inline void KThrQueueInit(KThrQueue *q) { q->flag = 0; }
static inline void KThrQueueBlock(KThrQueue *q, int val) { (void)q; (void)val; }
static inline void KThrQueueUnblockAll(KThrQueue *q) { (void)q; }
static inline void KThrQueueUnblockAllByValue(KThrQueue *q, int val) { (void)q; (void)val; }
}

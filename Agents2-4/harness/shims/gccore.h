// Host (PPC-Linux) shim for libogc <gccore.h> — only the handful of symbols the
// emulation core references. Implementations live in shims.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// libogc gctypes.h base typedefs (the Wii build gets these via <gccore.h>).
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef volatile uint8_t  vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;
typedef volatile uint64_t vu64;
typedef volatile int8_t   vs8;
typedef volatile int16_t  vs16;
typedef volatile int32_t  vs32;
typedef volatile int64_t  vs64;
typedef float  f32;
typedef double f64;
typedef volatile float  vf32;
typedef volatile double vf64;
typedef u32 uptr;
typedef s32 sptr;

#define _SHIFTL(v, s, w) ((u32)(((u32)(v) & ((0x01u << (w)) - 1u)) << (s)))
#define _SHIFTR(v, s, w) ((u32)(((u32)(v) >> (s)) & ((0x01u << (w)) - 1u)))

#define BIT(n) (1u << (n))

#ifdef __cplusplus
extern "C" {
#endif

void *Noods_MEM2_Alloc(size_t size);
void  Noods_MEM2_Free(void *ptr);

void DCFlushRange(void *start, uint32_t length);
void DCInvalidateRange(void *start, uint32_t length);
void ICInvalidateRange(void *start, uint32_t length);

void SYS_Report(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

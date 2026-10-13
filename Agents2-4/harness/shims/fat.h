// Host (PPC-Linux) shim for libogc <fat.h>.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int fatInitDefault(void);
void fatUnmount(const char *name);

#ifdef __cplusplus
}
#endif

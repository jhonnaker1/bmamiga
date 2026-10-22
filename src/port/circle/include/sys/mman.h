/*
 * sys/mman.h for Amiberry on Circle (src/port/circle/posix.c).
 *
 * There is no virtual memory: "mapping" is allocating from the heap, which
 * is readable, writable and executable, and so mprotect() does nothing.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_SYS_MMAN_H_
#define BMAMIGA_CIRCLE_SYS_MMAN_H_

#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_FAILED    ((void *) -1)

#define MADV_NORMAL   0
#define MADV_DONTNEED 4

#define MS_ASYNC      1
#define MS_SYNC       4

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t length, int prot);
int madvise(void *addr, size_t length, int advice);
int msync(void *addr, size_t length, int flags);
int mlock(const void *addr, size_t length);
int munlock(const void *addr, size_t length);
int shm_open(const char *name, int oflag, mode_t mode);
int shm_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* BMAMIGA_CIRCLE_SYS_MMAN_H_ */

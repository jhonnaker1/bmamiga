/*
 * utime.h for Amiberry on Circle: newlib's, with utime() declared.
 *
 * newlib has struct utimbuf but declares utime() only for systems that
 * have it. Lhasa, which Amiberry decodes LHA archives with, sets a file's
 * time with it; on the Pi it does nothing (src/port/circle/posix.c), as
 * chmod() and chown() do.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_UTIME_H_
#define BMAMIGA_CIRCLE_UTIME_H_

#include_next <utime.h>

#ifdef __cplusplus
extern "C" {
#endif

int utime(const char *path, const struct utimbuf *times);

#ifdef __cplusplus
}
#endif

#endif /* BMAMIGA_CIRCLE_UTIME_H_ */

/*
 * sys/dirent.h for Amiberry on Circle: circle-stdlib's, without FatFs's
 * header (wrap_fatfs.h), whose Windows style types (DWORD, BYTE...) clash
 * with Amiberry's. It was only needed for FF_LFN_BUF, the length of a
 * name, which is 255 in Circle's FatFs (addon/fatfs/ffconf.h): struct
 * dirent must stay the same as circle-stdlib's.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define BMAMIGA_FF_LFN_BUF 255

typedef struct _CIRCLE_DIR DIR;

struct dirent {
	ino_t d_ino;
	char d_name[BMAMIGA_FF_LFN_BUF + 1];
};

DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int readdir_r(DIR *__restrict, struct dirent *__restrict, struct dirent **__restrict);
void rewinddir(DIR *);
int closedir(DIR *);

#ifdef __cplusplus
}
#endif

#endif

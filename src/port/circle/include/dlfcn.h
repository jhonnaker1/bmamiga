/*
 * dlfcn.h for Amiberry on Circle: there are no shared libraries, so
 * dlopen() always fails (src/port/circle/posix.c).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_DLFCN_H_
#define BMAMIGA_CIRCLE_DLFCN_H_

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_LOCAL  0
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT ((void *) 0)

typedef struct {
	const char *dli_fname;
	void *dli_fbase;
	const char *dli_sname;
	void *dli_saddr;
} Dl_info;

#ifdef __cplusplus
extern "C" {
#endif

void *dlopen(const char *file, int mode);
void *dlsym(void *handle, const char *name);
int dlclose(void *handle);
char *dlerror(void);
int dladdr(const void *addr, Dl_info *info);

#ifdef __cplusplus
}
#endif

#endif /* BMAMIGA_CIRCLE_DLFCN_H_ */

/*
 * pty.h for Amiberry on Circle: there are no processes, so forkpty()
 * always fails (src/port/circle/posix.c).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_PTY_H_
#define BMAMIGA_CIRCLE_PTY_H_

#include <termios.h>
#include <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

int forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp);
int openpty(int *amaster, int *aslave, char *name, const struct termios *termp, const struct winsize *winp);

#ifdef __cplusplus
}
#endif

#endif /* BMAMIGA_CIRCLE_PTY_H_ */

/*
 * sys/ioctl.h for Amiberry on Circle: there are no devices to control, so
 * ioctl() fails (src/port/circle/posix.c).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_SYS_IOCTL_H_
#define BMAMIGA_CIRCLE_SYS_IOCTL_H_

#define FIONREAD   0x541b
#define FIONBIO    0x5421
#define TIOCGWINSZ 0x5413
#define TIOCMGET   0x5415
#define TIOCMSET   0x5418
#define TIOCMBIS   0x5416
#define TIOCMBIC   0x5417
#define TIOCSBRK   0x5427
#define TIOCCBRK   0x5428

struct winsize {
	unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
};

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* BMAMIGA_CIRCLE_SYS_IOCTL_H_ */

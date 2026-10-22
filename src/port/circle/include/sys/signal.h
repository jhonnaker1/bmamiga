/*
 * sys/signal.h for Amiberry on Circle: newlib's, with POSIX's struct
 * sigaction.
 *
 * newlib's struct sigaction has only sa_handler (sa_sigaction is in its
 * RTEMS version only), and amiberry.cpp installs its crash handlers with
 * sa_sigaction and SA_SIGINFO. newlib's is renamed out of the way and
 * POSIX's defined; Circle has no signals, so BMAmiga's sigaction()
 * (src/port/circle/posix.c) does nothing.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_SYS_SIGNAL_H_
#define BMAMIGA_CIRCLE_SYS_SIGNAL_H_

#define sigaction bmamiga_newlib_sigaction
#include_next <sys/signal.h>
#undef sigaction

#ifdef __cplusplus
extern "C" {
#endif

struct sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t *, void *);
	};
	sigset_t sa_mask;
	int sa_flags;
};

#ifndef SA_SIGINFO
#define SA_SIGINFO 0x2
#endif
#ifndef SA_ONSTACK
#define SA_ONSTACK 0x4
#endif
#ifndef SA_RESTART
#define SA_RESTART 0x10000000
#endif
#ifndef SA_NODEFER
#define SA_NODEFER 0x40000000
#endif
#ifndef SA_RESETHAND
#define SA_RESETHAND 0x80000000
#endif

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact);

#ifdef __cplusplus
}
#endif

#endif /* BMAMIGA_CIRCLE_SYS_SIGNAL_H_ */

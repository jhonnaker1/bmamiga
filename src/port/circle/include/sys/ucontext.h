/*
 * sys/ucontext.h for Amiberry on Circle: Linux's AArch64 layout, for its
 * crash handler (src/osdep/sigsegv_handler.cpp), which is never called:
 * there are no signals.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_SYS_UCONTEXT_H_
#define BMAMIGA_CIRCLE_SYS_UCONTEXT_H_

#include <signal.h>

typedef struct {
	unsigned long long fault_address;
	unsigned long long regs[31];
	unsigned long long sp;
	unsigned long long pc;
	unsigned long long pstate;
} mcontext_t;

typedef struct ucontext {
	unsigned long uc_flags;
	struct ucontext *uc_link;
	stack_t uc_stack;
	sigset_t uc_sigmask;
	mcontext_t uc_mcontext;
} ucontext_t;

#endif /* BMAMIGA_CIRCLE_SYS_UCONTEXT_H_ */

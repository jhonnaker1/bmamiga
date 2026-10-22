/*
 * nosignals.cpp - Amiberry's crash handlers on Circle, in place of
 * src/osdep/sigsegv_handler.cpp.
 *
 * Amiberry's catch the host's SIGSEGV and SIGBUS, mostly for its JIT (a
 * fault in its direct memory access is turned into a slow access).
 * Circle has no signals: a fault is an exception, which Circle reports
 * itself, and BMAmiga builds Amiberry without its JIT. These are what
 * amiberry.cpp installs (sigaction() does nothing), never called.
 *
 * Compiled with Amiberry's flags (src/port/amiberry.mk, BMA_EXTRA).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include <csignal>

void init_max_signals()
{
}

void signal_segv(int signum, siginfo_t *info, void *ptr)
{
	(void) info;
	(void) ptr;
	write_log(_T("Signal %d\n"), signum);
}

void signal_buserror(int signum, siginfo_t *info, void *ptr)
{
	signal_segv(signum, info, ptr);
}

void signal_term(int signum, siginfo_t *info, void *ptr)
{
	signal_segv(signum, info, ptr);
}

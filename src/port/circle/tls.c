/*
 * tls.c - thread-local storage for Amiberry on Circle.
 *
 * Amiberry has thread_local variables (inputdevice.cpp, its file system,
 * libco's current coroutine...). GCC reaches them from the thread pointer,
 * TPIDR_EL0 on AArch64: its 16 byte header, then the thread's copy of
 * .tdata (the initial values), then .tbss (zeros). Circle places those
 * sections (circle.ld) and clears .tbss, but sets no thread pointer. This
 * gives the emulator's core one block, shared by all of BMAmiga's threads
 * (circle/threads.c), and points TPIDR_EL0 at it.
 *
 * Where the copy goes after the header depends on how .tdata is aligned
 * (16 bytes, or more if a variable asks for it), so the block is checked
 * with a thread_local of this file's own, for each alignment in turn.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


extern unsigned char __tdata_start[], __tdata_end[], __tbss_start[], __tbss_end[];

/* Read through the thread pointer: volatile and external, so the compiler
   can't use its initial value instead. */
__thread volatile unsigned bma_tls_probe = 0x4c69736aU;

static void set_thread_pointer(void *p)
{
	__asm__ volatile("msr tpidr_el0, %0" : : "r"(p) : "memory");
}

static unsigned read_probe(void) __attribute__((noinline));
static unsigned read_probe(void)
{
	return bma_tls_probe;
}

/* Returns 0 if it can't. */
int bma_tls_init(void)
{
	size_t tdata = (size_t) (__tdata_end - __tdata_start);
	size_t tbss = (size_t) (__tbss_end - __tbss_start);
	size_t align;
	for (align = 16; align <= 4096; align *= 2) {
		/* The header (16 bytes) rounded up to ALIGN, then the data. */
		size_t offset = align, size = offset + tdata + align + tbss;
		/* posix.c's: Circle's memalign() asserts past 64 bytes. */
		unsigned char *block;
		if (posix_memalign((void **) &block, align, size) != 0)
			return 0;
		memset(block, 0, size);
		memcpy(block + offset, __tdata_start, tdata);
		set_thread_pointer(block);
		if (read_probe() == 0x4c69736aU) {
			/* newlib's printf has no %zu (it prints "zu") */
			printf("BMAmiga: thread-local storage %lu + %lu bytes, aligned to %lu\n",
			       (unsigned long) tdata, (unsigned long) tbss, (unsigned long) align);
			return 1;
		}
		set_thread_pointer(NULL);
		free(block);
	}
	printf("BMAmiga: cannot set up thread-local storage\n");
	return 0;
}

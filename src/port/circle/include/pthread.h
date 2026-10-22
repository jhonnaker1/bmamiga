/*
 * pthread.h for Amiberry on Circle: newlib's, with its functions declared.
 *
 * newlib declares the pthread functions only where _POSIX_THREADS is
 * defined, which it isn't for Circle (it has no threads). BMAmiga has its
 * own, cooperative ones (src/port/circle/threads.c), so they are declared.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_CIRCLE_PTHREAD_H_
#define BMAMIGA_CIRCLE_PTHREAD_H_

#ifndef _POSIX_THREADS
#define _POSIX_THREADS 1
#endif
#ifndef _POSIX_TIMEOUTS
#define _POSIX_TIMEOUTS 1
#endif
#include_next <pthread.h>

#endif /* BMAMIGA_CIRCLE_PTHREAD_H_ */

/*
 * clock.c - Amiberry's time and sleeps, on the host's clock.
 *
 * Amiberry times the Amiga with clock_gettime(CLOCK_MONOTONIC) (src/
 * machdep/support.cpp) and waits with nanosleep(), usleep() and
 * SDL_Delay(). The core is compiled with those renamed to the functions
 * below (BMA_FLAGS in the Makefiles), so that its time is host_time(): on
 * the Pi that is Circle's timer, and in the PC simulator a virtual clock,
 * which lets the tests run as fast as the PC can. The time of day
 * (CLOCK_REALTIME, the Amiga's clock and timed waits) stays the real one.
 *
 * Pacing (with its internal vsync off; on, since Amiberry #2629, it doesn't
 * wait at all and the display paces it): Amiberry's libretro core paces
 * itself, waiting at the end
 * of each frame until one Amiga frame (20.03 ms for PAL) has passed since
 * it last waited (isvsync_chipset() is 0 in libretro builds; framewait()
 * in custom.cpp). Locked to a 50 Hz display that is never quite right: its
 * wait creeps later by the 0.03 ms a frame between the two, until a frame
 * misses its display refresh. So while the display paces BMAmiga
 * (platform.c calls bmamiga_clock_frame() after each refresh), Amiberry's
 * time is the frames': it moves on exactly one Amiga frame per frame shown
 * (and as the clock between), and Amiberry waits only if a frame was
 * quicker than the slowest one before it. Paced by the timer, its time is
 * the host's. It never goes backwards.
 *
 * A loop spinning on the clock until a moment comes would never end in the
 * simulator, where time only passes when something lets it: reading the
 * same time over and over (1024 times) counts as having waited 10 us, which
 * on the Pi is about what such a loop takes anyway.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "../host.h"

void bma_threads_yield(void);   /* circle/threads.c */
int bma_threads_sleep(double seconds);

static double offset;       /* Amiberry's time = host_time() + offset */
double bmamiga_clock_slept; /* seconds Amiberry has slept, for the load */
static double last_given;   /* the time last given, which it never goes below */
static int frame_paced;
static double frame_time;   /* Amiberry's time at the start of this frame */

void bmamiga_clock_frame(int display_paces, double period)
{
	double now = host_time();
	if (display_paces && period > 0.0) {
		if (!frame_paced)
			frame_time = now + offset;
		else
			frame_time += period;
		if (frame_time < last_given)
			frame_time = last_given;
		offset = frame_time - now;
	}
	frame_paced = display_paces;
}

static int is_monotonic(clockid_t id)
{
	if (id == CLOCK_MONOTONIC)
		return 1;
#ifdef CLOCK_MONOTONIC_RAW
	if (id == CLOCK_MONOTONIC_RAW)
		return 1;
#endif
	return 0;
}

int bma_clock_gettime(clockid_t id, struct timespec *ts)
{
	static double last_host;
	static unsigned same;
	double host, t;
	if (!is_monotonic(id))
		return clock_gettime(id, ts);
	host = host_time();
	if (host == last_host && ++same >= 1024) {
		same = 0;
		host_sleep_us(10);
		host = host_time();
	} else if (host != last_host) {
		same = 0;
	}
	last_host = host;
	t = host + offset;
	if (t < last_given)
		t = last_given;
	last_given = t;
	ts->tv_sec = (time_t) t;
	ts->tv_nsec = (long) ((t - (double) ts->tv_sec) * 1e9);
	return 0;
}

#ifdef __APPLE__
uint64_t bma_clock_gettime_nsec_np(clockid_t id)
{
	struct timespec ts;
	bma_clock_gettime(id, &ts);
	return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}
#endif

int bma_nanosleep(const struct timespec *req, struct timespec *rem)
{
	double us;
	if (req == NULL || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
		errno = EINVAL;
		return -1;
	}
	us = (double) req->tv_sec * 1e6 + req->tv_nsec / 1e3;
	if (rem != NULL) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	/* BMAmiga's threads are cooperative: a sleep lets the others run. */
	if (bma_threads_sleep(us / 1e6))
		return 0;
	bma_threads_yield();
	bmamiga_clock_slept += us / 1e6;
	host_sleep_us(us > 4e9 ? 4000000000u : (unsigned) us);
	return 0;
}

int bma_usleep(useconds_t us)
{
	if (bma_threads_sleep(us / 1e6))
		return 0;
	bma_threads_yield();
	bmamiga_clock_slept += us / 1e6;
	host_sleep_us((unsigned) us);
	return 0;
}

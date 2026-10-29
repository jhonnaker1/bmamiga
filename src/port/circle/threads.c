/*
 * threads.c - POSIX threads for Amiberry, cooperative, on libco.
 *
 * Circle has no threads, and newlib's pthread functions are only declared.
 * Amiberry starts its threads through its SDL stand-in (libretro/
 * sdl_stub.cpp, pthreads underneath): under libretro only "denise", the
 * drawing thread, which waits for work that never comes, as libretro
 * builds draw in the emulation itself (multithread_enabled is 0). So these
 * threads are coroutines (Amiberry's libco, which libretro already runs
 * the emulation on), all on the emulator's core: a thread runs until it
 * waits (a mutex someone else holds, a condition, a join, a sleep), then
 * the next one that can go on runs, in turn. Nothing is preempted.
 *
 * Mutexes and conditions, and std::mutex and std::condition_variable
 * (bma_object_*, src/port/circle/include/bma_circle.h), are found by
 * their address, not kept in the object: newlib's pthread_mutex_t is 32
 * bits, macOS's a large struct, and so the PC simulator runs these too
 * (it renames Amiberry's pthread calls to bma_pthread_*, see its
 * Makefile). Mutexes are recursive, as SDL's are.
 *
 * A thread that waits with nothing else able to go on: a timed wait times
 * out at once and a sleep returns (the time is the emulator's to spend);
 * an untimed one is a deadlock, which is logged and returns.
 *
 * The threads share one thread-local storage block (as the libretro core
 * and its emulation coroutine do on the desktop); circle/tls.c sets it up.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libco.h"

int bma_clock_gettime(clockid_t id, struct timespec *ts);   /* clock.c */

#define MAX_THREADS 64
#define THREAD_STACK (256 * 1024)

struct thread;
typedef int (*ready_fn)(struct thread *t);

struct thread {
	int used;
	cothread_t co;
	int own;                    /* started by pthread_create() */
	void *(*start)(void *);
	void *arg;
	void *result;
	int finished;
	int detached;
	int in_scheduler;           /* waiting or yielding: may be switched to */
	/* What it waits for: NULL is nothing (it can run). */
	ready_fn ready;
	const void *wait_object;
	unsigned wait_generation;
	int wait_thread;
	double deadline;            /* seconds (bma_clock_gettime's), 0 none */
};

static struct thread threads[MAX_THREADS];

/* ---- Time --------------------------------------------------------------- */

static double now_seconds(void)
{
	struct timespec ts;
	bma_clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double) ts.tv_sec + ts.tv_nsec / 1e9;
}

/* An absolute CLOCK_REALTIME time (pthread_cond_timedwait()'s), as a
   deadline on the monotonic clock. */
static double deadline_from_abstime(const struct timespec *abstime)
{
	struct timespec real;
	double left;
	bma_clock_gettime(CLOCK_REALTIME, &real);
	left = (double) (abstime->tv_sec - real.tv_sec) + (abstime->tv_nsec - real.tv_nsec) / 1e9;
	return now_seconds() + (left > 0.0 ? left : 0.0);
}

/* ---- Threads ------------------------------------------------------------ */

/* The thread running: a coroutine not started here (the first caller, the
   libretro core's emulation coroutine) is given a record when first seen. */
static int current_index(void)
{
	cothread_t co = co_active();
	int i, free_slot = -1;
	for (i = 0; i < MAX_THREADS; i++) {
		if (threads[i].used && threads[i].co == co)
			return i;
		if (!threads[i].used && free_slot < 0)
			free_slot = i;
	}
	if (free_slot < 0) {
		fprintf(stderr, "BMAmiga: too many threads\n");
		abort();
	}
	memset(&threads[free_slot], 0, sizeof threads[free_slot]);
	threads[free_slot].used = 1;
	threads[free_slot].co = co;
	return free_slot;
}

/* A coroutine not started here (the libretro core's, its emulation's)
   is only switched back to when it is in here, waiting or yielding: it is
   otherwise in libretro's own co_switch(), where it must stay. */
static int can_run(struct thread *t)
{
	if (!t->used || t->finished)
		return 0;
	if (!t->own && !t->in_scheduler)
		return 0;
	if (t->ready == NULL || t->ready(t))
		return 1;
	return t->deadline > 0.0 && now_seconds() >= t->deadline;
}

/* Frees a detached thread that finished (not the one running). */
static void reap(int self)
{
	int i;
	for (i = 0; i < MAX_THREADS; i++)
		if (i != self && threads[i].used && threads[i].own && threads[i].finished
		    && threads[i].detached) {
			co_delete(threads[i].co);
			threads[i].used = 0;
		}
}

/* Runs the next thread, after SELF, that can go on. Returns 0 if there is
   none (SELF goes on), 1 once SELF runs again. */
static int switch_away(int self)
{
	int k, r = 0;
	reap(self);
	threads[self].in_scheduler = 1;
	for (k = 1; k < MAX_THREADS; k++) {
		int i = (self + k) % MAX_THREADS;
		if (can_run(&threads[i])) {
			co_switch(threads[i].co);
			r = 1;
			break;
		}
	}
	threads[self].in_scheduler = 0;
	return r;
}

/* Waits until READY says SELF can go on, or DEADLINE (0 none). Returns 0,
   or ETIMEDOUT. */
static int wait_for(int self, ready_fn ready, double deadline)
{
	struct thread *t = &threads[self];
	int r = 0;
	t->ready = ready;
	t->deadline = deadline;
	for (;;) {
		if (ready(t))
			break;
		if (deadline > 0.0 && now_seconds() >= deadline) {
			r = ETIMEDOUT;
			break;
		}
		if (!switch_away(self)) {
			/* Nothing else can run. */
			if (deadline > 0.0) {
				r = ETIMEDOUT;
			} else {
				static int said;
				if (!said++)
					fprintf(stderr, "BMAmiga: threads deadlocked\n");
				r = EDEADLK;
			}
			break;
		}
	}
	t->ready = NULL;
	t->deadline = 0.0;
	return r;
}

/* Lets the others run until they wait (the platform calls this once a
   frame, and sleeps do). */
void bma_threads_yield(void)
{
	switch_away(current_index());
}

static void thread_entry(void)
{
	int self = current_index();
	struct thread *t = &threads[self];
	t->result = t->start(t->arg);
	t->finished = 1;
	for (;;) {
		if (!switch_away(self)) {
			fprintf(stderr, "BMAmiga: a thread ended with no other to run\n");
			abort();
		}
	}
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg)
{
	int i;
	(void) attr;
	current_index();            /* the caller has a record */
	for (i = 0; i < MAX_THREADS; i++)
		if (!threads[i].used)
			break;
	if (i == MAX_THREADS)
		return EAGAIN;
	memset(&threads[i], 0, sizeof threads[i]);
	threads[i].co = co_create(THREAD_STACK, thread_entry);
	if (threads[i].co == NULL)
		return EAGAIN;
	threads[i].used = 1;
	threads[i].own = 1;
	threads[i].start = start;
	threads[i].arg = arg;
	*thread = (pthread_t) (uintptr_t) (i + 1);
	return 0;
}

static int thread_finished(struct thread *t)
{
	return threads[t->wait_thread].finished;
}

int pthread_join(pthread_t thread, void **retval)
{
	int i = (int) (uintptr_t) thread - 1, self = current_index();
	if (i < 0 || i >= MAX_THREADS || !threads[i].used || !threads[i].own || i == self)
		return EINVAL;
	threads[self].wait_thread = i;
	if (wait_for(self, thread_finished, 0.0) != 0)
		return EDEADLK;
	if (retval != NULL)
		*retval = threads[i].result;
	co_delete(threads[i].co);
	threads[i].used = 0;
	return 0;
}

int pthread_detach(pthread_t thread)
{
	int i = (int) (uintptr_t) thread - 1;
	if (i < 0 || i >= MAX_THREADS || !threads[i].used)
		return EINVAL;
	threads[i].detached = 1;
	return 0;
}

pthread_t pthread_self(void)
{
	return (pthread_t) (uintptr_t) (current_index() + 1);
}

int sched_yield(void)
{
	bma_threads_yield();
	return 0;
}

/* ---- Mutexes and conditions, by address ------------------------------- */

#define MAX_OBJECTS 8192            /* a power of two */
#define TOMBSTONE ((const void *) 1)

struct object {
	const void *key;
	int owner;                  /* thread + 1, 0 unlocked */
	int count;
	unsigned generation;        /* conditions: notifies so far */
};

static struct object objects[MAX_OBJECTS];

static unsigned hash(const void *p)
{
	uintptr_t v = (uintptr_t) p;
	v ^= v >> 17;
	v *= 0x9e3779b97f4a7c15ull;
	return (unsigned) (v >> 32) & (MAX_OBJECTS - 1);
}

static struct object *find(const void *key, int create)
{
	unsigned h = hash(key), n;
	struct object *tomb = NULL;
	for (n = 0; n < MAX_OBJECTS; n++, h = (h + 1) & (MAX_OBJECTS - 1)) {
		struct object *o = &objects[h];
		if (o->key == key)
			return o;
		if (o->key == TOMBSTONE && tomb == NULL)
			tomb = o;
		if (o->key == NULL) {
			if (!create)
				return NULL;
			if (tomb != NULL)
				o = tomb;
			memset(o, 0, sizeof *o);
			o->key = key;
			return o;
		}
	}
	if (create && tomb != NULL) {
		memset(tomb, 0, sizeof *tomb);
		tomb->key = key;
		return tomb;
	}
	if (create) {
		fprintf(stderr, "BMAmiga: too many mutexes\n");
		abort();
	}
	return NULL;
}

void bma_object_forget(const void *object)
{
	struct object *o = find(object, 0);
	if (o != NULL)
		o->key = TOMBSTONE;
}

static int lock_free(struct thread *t)
{
	struct object *o = find(t->wait_object, 0);
	return o == NULL || o->owner == 0;
}

void bma_object_lock(const void *object)
{
	int self = current_index();
	struct object *o = find(object, 1);
	if (o->owner == self + 1) {
		o->count++;
		return;
	}
	if (o->owner != 0) {
		threads[self].wait_object = object;
		wait_for(self, lock_free, 0.0);
		o = find(object, 1);
	}
	o->owner = self + 1;
	o->count = 1;
}

int bma_object_trylock(const void *object)
{
	int self = current_index();
	struct object *o = find(object, 1);
	if (o->owner == self + 1) {
		o->count++;
		return 1;
	}
	if (o->owner != 0)
		return 0;
	o->owner = self + 1;
	o->count = 1;
	return 1;
}

/* Set when a thread waiting on a condition has been notified: it runs as
   soon as the notifier lets go of its lock (below). */
static int wake_pending;

void bma_object_unlock(const void *object)
{
	struct object *o = find(object, 0);
	if (o == NULL || o->owner == 0)
		return;
	if (--o->count <= 0) {
		o->owner = 0;
		o->count = 0;
		/* A thread was woken (a semaphore posted: a lock, a notify, an
		   unlock, as pthreads have it): it runs now, until it waits
		   again, rather than whenever this one next waits or sleeps,
		   which the emulation may not do for a long time (Amiberry's
		   hard drive thread answered nothing while the Amiga waited for
		   it). So a request handed to a thread is answered at once. */
		if (wake_pending) {
			wake_pending = 0;
			switch_away(current_index());
		}
	}
}

static int notified(struct thread *t)
{
	struct object *o = find(t->wait_object, 0);
	return o == NULL || o->generation != t->wait_generation;
}

/* Waits for CONDITION with LOCK (locked once by the caller) unlocked
   meanwhile; DEADLINE 0 is none. */
static int wait_condition(const void *condition, const void *lock, double deadline)
{
	int self = current_index(), r, count = 1;
	struct object *c = find(condition, 1), *m = find(lock, 1);
	if (m->owner == self + 1) {
		count = m->count;
		m->owner = 0;
		m->count = 0;
	}
	threads[self].wait_object = condition;
	threads[self].wait_generation = c->generation;
	r = wait_for(self, notified, deadline);
	bma_object_lock(lock);
	m = find(lock, 1);
	m->count = count;
	return r;
}

int bma_object_wait(const void *condition, const void *lock, long long ms)
{
	double deadline = ms < 0 ? 0.0 : now_seconds() + ms / 1000.0;
	return wait_condition(condition, lock, deadline) == ETIMEDOUT;
}

void bma_object_notify(const void *condition, int all)
{
	struct object *c = find(condition, 1);
	int i;
	(void) all;                 /* every waiter wakes: spurious wakeups are allowed */
	c->generation++;
	for (i = 0; i < MAX_THREADS; i++)
		if (threads[i].used && threads[i].ready == notified && threads[i].wait_object == condition)
			wake_pending = 1;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	struct object *o = find(mutex, 1);
	(void) attr;
	o->owner = 0;
	o->count = 0;
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	bma_object_forget(mutex);
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	bma_object_lock(mutex);
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	return bma_object_trylock(mutex) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	bma_object_unlock(mutex);
	return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
	(void) attr;
	find(cond, 1)->generation = 0;
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
	bma_object_forget(cond);
	return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
	bma_object_notify(cond, 0);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
	bma_object_notify(cond, 1);
	return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	return wait_condition(cond, mutex, 0.0) == EDEADLK ? EDEADLK : 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
	int r = wait_condition(cond, mutex, deadline_from_abstime(abstime));
	return r == ETIMEDOUT ? ETIMEDOUT : 0;
}

/* ---- Sleeping ------------------------------------------------------------ */

static int never(struct thread *t)
{
	(void) t;
	return 0;
}

/* A sleep of SECONDS (clock.c's sleeps call this). One of Amiberry's own
   threads (the CD32's, the CD's audio, which poll every few milliseconds)
   waits here until its time is up while the others, the emulation too,
   go on: sleeping for real would stop the Amiga too (the CD32 ran at half
   speed). Returns 0 for the emulation's own thread (the libretro core's),
   which the caller sleeps for real: that is how it keeps time. */
int bma_threads_sleep(double seconds)
{
	int self = current_index();
	if (!threads[self].own)
		return 0;
	if (seconds <= 0.0)
		bma_threads_yield();
	else
		wait_for(self, never, now_seconds() + seconds);
	return 1;
}

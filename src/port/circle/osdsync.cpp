/*
 * osdsync.cpp - CHD's work queues for Amiberry on Circle, in place of
 * src/archivers/chd/osdsync.cpp.
 *
 * MAME's work queues run work items on threads of their own (std::thread),
 * which Circle hasn't. These run each item when it is queued, on the
 * caller's thread: the CHD code queues work and then waits for it, so the
 * result is the same, done in order. Only making CHD files uses them
 * (compressing hunks); reading one doesn't.
 *
 * Compiled with Amiberry's flags (src/port/amiberry.mk, BMA_EXTRA).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include "archivers/chd/osdcore.h"
#include "archivers/chd/osdsync.h"

#include <cstdint>

struct osd_work_queue {
	int flags;
	int pending;
};

struct osd_work_item {
	void *result;
	bool auto_release;
};

int osd_num_processors = 1;

osd_work_queue *osd_work_queue_alloc(int flags)
{
	osd_work_queue *queue = new osd_work_queue;
	queue->flags = flags;
	queue->pending = 0;
	return queue;
}

int osd_work_queue_items(osd_work_queue *queue)
{
	return queue->pending;
}

bool osd_work_queue_wait(osd_work_queue *queue, osd_ticks_t timeout)
{
	(void) queue;
	(void) timeout;
	return true;
}

void osd_work_queue_free(osd_work_queue *queue)
{
	delete queue;
}

/* Runs NUMITEMS items at once, the parameters PARAMSTEP bytes apart, and
   returns the last (as MAME's does). */
osd_work_item *osd_work_item_queue_multiple(osd_work_queue *queue, osd_work_callback callback,
                                            int32_t numitems, void *parambase, int32_t paramstep,
                                            uint32_t flags)
{
	osd_work_item *last = nullptr;
	(void) queue;
	for (int32_t i = 0; i < numitems; i++) {
		void *param = static_cast<uint8_t *>(parambase) + static_cast<intptr_t>(i) * paramstep;
		void *result = callback(param, 0);
		if (flags & WORK_ITEM_FLAG_AUTO_RELEASE)
			continue;
		if (last != nullptr)
			delete last;
		last = new osd_work_item;
		last->result = result;
		last->auto_release = false;
	}
	return last;
}

bool osd_work_item_wait(osd_work_item *item, osd_ticks_t timeout)
{
	(void) item;
	(void) timeout;
	return true;
}

void *osd_work_item_result(osd_work_item *item)
{
	return item != nullptr ? item->result : nullptr;
}

void osd_work_item_release(osd_work_item *item)
{
	delete item;
}

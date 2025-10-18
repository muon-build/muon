/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "arena.h"
#include "datastructures/seg_list.h"
#include "platform/assert.h"
#include "util.h"

/* Make the first segment 2 ^ SLIST_BASE_POW items long. */
#define SLIST_BASE_POW 6u
#define SLIST_BASE (uint32_t)(1 << SLIST_BASE_POW)

static uint32_t
sl_log2i(uint32_t i)
{
#if defined(__GNUC__) && 0
	return 63 - __builtin_clzll((unsigned long long)i);
#else
	// right propogate
	i = i | (i >> 1);
	i = i | (i >> 2);
	i = i | (i >> 4);
	i = i | (i >> 8);
	i = i | (i >> 16);
	i = ~i;

	// count 1-bits
	i = i - ((i >> 1) & 0x55555555);
	i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
	i = (i + (i >> 4)) & 0x0f0f0f0f;
	i = i + (i >> 8);
	i = i + (i >> 16);
	i = i & 0x0000003f;

	// convert to log2
	i = 31 - i;
	return i;
#endif
}

uint32_t
sl_segment_count_for_capacity(uint32_t capacity)
{
	assert(IS_POWER_OF_TWO(capacity));
	return capacity < SLIST_BASE ? 1 : sl_log2i((capacity / SLIST_BASE)) + 1;
}

uint32_t
sl_slots_in_segment(uint32_t segment_index)
{
	return SLIST_BASE << (segment_index > 1 ? segment_index - 1 : 0);
}

uint32_t
sl_capacity_for_segment_count(uint32_t segment_count)
{
	return segment_count > 1 ? (SLIST_BASE << (segment_count - 1)) : (SLIST_BASE * segment_count);
}

void *
sl_get_(struct slist *sl, uint32_t i, uint32_t item_size)
{
	assert(i < sl->len);
	const uint32_t seg = i < SLIST_BASE ? 0 : sl_log2i(((i - SLIST_BASE) >> SLIST_BASE_POW) + 1) + 1;
	const uint32_t slot = i - sl_capacity_for_segment_count(seg);
	return ((char *)sl->segments[seg]) + item_size * slot;
}

void sl_grow_to_(struct arena *a, struct slist *sl, uint32_t size, uint32_t item_size, uint32_t item_align, uint32_t max_segments)
{
	sl->len += size;
	const uint32_t new_segs_used = sl_segment_count_for_capacity(sl->len);
	assert(new_segs_used <= max_segments);
	for (; sl->segs_used < new_segs_used; ++sl->segs_used) {
		sl->segments[sl->segs_used]
			= ar_alloc(a, sl_slots_in_segment(sl->segs_used), item_size, item_align);
	}
}

void *sl_alloc_(struct arena *a, struct slist *sl, uint32_t item_size, uint32_t item_align, uint32_t max_segments)
{
	// create the next segment if needed
	if (sl->len >= sl_capacity_for_segment_count(sl->segs_used)) {
		assert(sl->segs_used < max_segments);
		sl->segments[sl->segs_used]
			= ar_alloc(a, sl_slots_in_segment(sl->segs_used), item_size, item_align);
		++sl->segs_used;
	}

	++sl->len;
	return sl_get_(sl, sl->len - 1, item_size);
}

void *sl_push_(struct arena *a, struct slist *sl, const void *e, uint32_t item_size, uint32_t item_align, uint32_t max_segments)
{
	void *r = sl_alloc_(a, sl, item_size, item_align, max_segments);
	memcpy(r, e, item_size);
	return r;
}

void
sl_clear_(struct slist *sl)
{
	sl->len = 0;
}

void
sl_memset_(struct slist *sl, uint8_t c)
{
	for (uint32_t i = 0; i < sl->segs_used; ++i) {
		memset(sl->segments[i], c, sl_slots_in_segment(i));
	}
}

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_DATA_SEG_LIST_H
#define MUON_DATA_SEG_LIST_H

#include <stdint.h>
#include <string.h>

#include "buf_size.h"

struct slist {
	uint32_t len;
	uint32_t segs_used;
	void *segments[19];
};

struct arena;


void *sl_get_(const struct slist *sl, uint32_t i, uint32_t item_size);
#define sl_get(__sl, __i, __type) (__type *)sl_get_(__sl, __i, sizeof(__type))

void sl_grow_to_(struct arena *a, struct slist *sl, uint32_t size, uint32_t item_size, uint32_t item_align, uint32_t max_segments);
#define sl_grow_to(__a, __sl, __size, __type) sl_grow_to_(__a, __sl, __size, sizeof(__type), ar_alignof(__type), ARRAY_LEN((__sl)->segments))

void *sl_alloc_(struct arena *a, struct slist *sl, uint32_t item_size, uint32_t item_align, uint32_t max_segments);
#define sl_alloc(__a, __sl, __type) (__type *)sl_alloc_(__a, __sl, sizeof(__type), ar_alignof(__type), ARRAY_LEN((__sl)->segments))

void *sl_push_(struct arena *a, struct slist *sl, const void *e, uint32_t item_size, uint32_t item_align, uint32_t max_segments);
#define sl_push(__a, __sl, __e, __type) (__type *)sl_push_(__a, __sl, __e, sizeof(__type), ar_alignof(__type), ARRAY_LEN((__sl)->segments))

void sl_del_(struct slist *sl, uint64_t i, uint32_t item_size);
#define sl_del(__sl, __i, __type) sl_del_(__sl, __i, sizeof(__type))

void sl_clear(struct slist *sl);
void sl_memset(struct slist *sl, uint8_t c);
uint32_t sl_slots_in_segment(uint32_t segment_index);
uint32_t sl_capacity_for_segment_count(uint32_t segment_count);
uint32_t sl_segment_count_for_capacity(uint32_t capacity);

#define sl_for_named(__sl, __type, __it, ...) {                                                   \
	struct {                                                                                  \
		uint32_t seg, slot, idx;                                                          \
		__type *it;                                                                       \
	} __it = { 0 };                                                                           \
	for (; __it.seg < (__sl)->segs_used; ++__it.seg) {                                        \
		for (__it.slot = 0, __it.it = &((__type *)(__sl)->segments[__it.seg])[__it.slot]; \
			__it.slot < sl_slots_in_segment(__it.seg) && __it.idx < (__sl)->len       \
			&& (__it.it = &((__type *)(__sl)->segments[__it.seg])[__it.slot], 1);     \
			++__it.slot, ++__it.idx) {                                                \
			__VA_ARGS__                                                               \
		}                                                                                 \
	}                                                                                         \
}

#define sl_for(__sl, __type, ...) sl_for_named(__sl, __type, it, __VA_ARGS__)

#endif

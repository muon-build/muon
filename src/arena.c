/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdbool.h>
#include <string.h>

#ifdef TRACY_ENABLE
#include <stdio.h>
#endif

#include "arena.h"
#include "platform/assert.h"
#include "platform/mem.h"
#include "tracy.h"

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define asan_poison_memory_region(addr, size) __asan_poison_memory_region((addr), (size))
#define asan_unpoison_memory_region(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#define asan_poison_memory_region(addr, size) ((void)(addr), (void)(size))
#define asan_unpoison_memory_region(addr, size) ((void)(addr), (void)(size))
#endif

enum ar_flag {
	ar_flag_fixed = 1 << 0,
};

struct ar_block {
	struct ar_block *next, *prev;
	char *end;
	uint64_t size;
	char start[];
};

static int64_t
ar_block_len_full(struct ar_block *b)
{
	return b->end - b->start;
}

static int64_t
ar_block_len_free(struct ar_block *b)
{
	return b->size - ar_block_len_full(b);
}

static void
ar_block_clear(struct ar_block *b)
{
	b->end = b->start;
	asan_poison_memory_region(b->start, b->size);
}

static void
ar_block_init(struct ar_block *b, struct ar_block *prev, uint64_t block_size)
{
	*b = (struct ar_block){
		.prev = prev,
		.size = block_size,
	};

	ar_block_clear(b);
}

static struct ar_block *
ar_block_alloc(const struct arena *a, uint64_t count, uint64_t objsize, struct ar_block *prev)
{
	TracyCZoneAutoS;
	uint64_t block_size = a->params.block_size, size = count * objsize;
	while (size > block_size) {
		block_size *= 2;
	}
	if (block_size > a->params.block_size) {
#ifdef TRACY_ENABLE
		const char *msg = "allocating large block";
		TracyCMessage(msg, strlen(msg));
#endif
	}

	uint64_t s = sizeof(struct ar_block) + block_size;
	struct ar_block *b = z_malloc(s);
	TracyCAllocN(b, s, a->tracy_tag);
	ar_block_init(b, prev, block_size);
	TracyCZoneAutoE;
	return b;
}

static void
ar_block_free(struct arena *a, struct ar_block *block)
{
	TracyCFreeN(block, a->tracy_tag);
	z_free(block);
}

void
ar_init(struct arena *a, const struct ar_params *params)
{
	*a = (struct arena){ .params = *params };

	if (!a->params.block_size) {
		a->params.block_size = (4 * 1024 * 1024) - sizeof(struct ar_block);
	}
	assert(a->params.block_size);

#ifdef TRACY_ENABLE
	static int id = 0;
	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), "arena-%d", id);
	a->tracy_tag = strdup(buf); // Leaked and not tracked
	++id;
#endif
}

void *
ar_alloc(struct arena *a, uint64_t count, uint64_t objsize, uint64_t align)
{
	assert(count && align);

	if (!a->tail) {
		a->head = a->tail = ar_block_alloc(a, count, objsize, 0);
	}

	int64_t pad, size_unpadded, size;
retry:
	pad = ((uint64_t)a->tail->end & (align - 1));
	pad = pad ? align - pad : 0;
	size_unpadded = objsize * count;
	size = size_unpadded + pad;
	if (size > ar_block_len_free(a->tail)) {
		if (a->params.flags & ar_flag_fixed) {
			assert(false && "fixed arena OOM");
		}

		struct ar_block *new_block = ar_block_alloc(a, count, objsize, a->tail);
		if (ar_block_len_full(a->tail)) {
			a->tail->next = new_block;
		} else {
			// We can get here if tail was an allocated but fully empty block
			// that was still too small to hold the requested allocation.  Just
			// free it so we don't get empty blocks in the chain.
			new_block->prev = a->tail->prev;
			ar_block_free(a, a->tail);
			if (a->tail == a->head) {
				a->head = new_block;
			}
		}

		a->tail = new_block;
		goto retry;
	}

	a->pos += size;

	void *mem = a->tail->end + pad;
	//TracyCAllocN(mem, size_unpadded, tracy_arena_block_mem_tag);

	a->tail->end += size;
	asan_unpoison_memory_region(mem, size_unpadded);
	memset(mem, 0, size_unpadded);
	return mem;
}

void
ar_clear(struct arena *a)
{
	if (!a->head) {
		return;
	}

	struct ar_block *b = a->head->next;
	while (b) {
		struct ar_block *next = b->next;
		ar_block_free(a, b);
		b = next;
	}

	a->head->next = 0;
	a->tail = a->head;
	ar_block_clear(a->head);
	a->pos = 0;
}

void
ar_destroy(struct arena *a)
{
	if (!a->head) {
		return;
	}

	ar_clear(a);
	ar_block_free(a, a->head);
	a->head = 0;
}

static void
ar_pop_tail_block(struct arena *a)
{
	struct ar_block *old_tail = a->tail;

	a->pos -= ar_block_len_full(old_tail);
	a->tail = old_tail->prev;
	ar_block_free(a, old_tail);
	a->tail->next = 0;
}

void
ar_pop_to(struct arena *a, int64_t want_pos)
{
	if (want_pos == a->pos) {
		return;
	}

	assert(want_pos <= a->pos);

	while (a->pos >= ar_block_len_full(a->tail) && want_pos < a->pos - ar_block_len_full(a->tail)) {
		ar_pop_tail_block(a);
	}

	uint64_t block_pos = ar_block_len_full(a->tail);
	uint64_t block_want_pos = want_pos - (a->pos - ar_block_len_full(a->tail));
	assert(a->tail->size >= block_pos);
	assert(block_pos >= block_want_pos);

	a->tail->end = a->tail->start + block_want_pos;
	asan_poison_memory_region(a->tail->end, ar_block_len_free(a->tail));
	a->pos = want_pos;
}

void
ar_init_fixed(struct arena *a, char *mem, uint64_t size, struct ar_params *params)
{
	ar_init(a, params);

	a->params.block_size = size;
	a->params.flags |= ar_flag_fixed;

	assert(size > sizeof(struct ar_block));
	struct ar_block *b = (struct ar_block *)mem;
	ar_block_init(b, 0, size - sizeof(struct ar_block));

	a->head = b;
	a->tail = a->head;
}

void *
ar_realloc(struct arena *a, void *ptr, int64_t original_size, int64_t new_size, uint64_t align)
{
	if (!new_size) {
		return 0;
	}

	if (new_size <= original_size) {
		return ptr;
	}

	bool have_ptr = ptr && original_size;
	bool resizeable = a->tail // This arena has allocations
			  && a->tail->end == (char *)ptr + original_size // ptr is the last allocation in this arena
			  && (new_size - original_size) <= ar_block_len_free(a->tail);

	if (resizeable) {
		ar_alloc(a, 1, new_size - original_size, 1);
		return ptr;
	} else {
		void *res = ar_alloc(a, 1, new_size, align);

		if (have_ptr && res) {
			memcpy(res, ptr, original_size);
		}

		return res;
	}
}

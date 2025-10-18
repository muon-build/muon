/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_ARENA_H
#define MUON_ARENA_H

#include <stdint.h>

#define ar_alignof(__type) (sizeof(struct{char c; __type d; }) - sizeof(__type))

struct ar_params {
	const char *source_file;
	uint32_t source_line;
	uint32_t flags;
	uint64_t block_size;
};

#if defined(TRACY_ENABLE)
#define ARENA_TRACE 1
#else
#define ARENA_TRACE 0
#endif

struct arena {
	struct ar_params params;
	struct ar_block *head, *tail;
	int64_t pos;
#if ARENA_TRACE
	struct ar_trace *trace;
#endif
};

void ar_init(struct arena *a, const struct ar_params *params);
void *ar_alloc(struct arena *a, uint64_t count, uint64_t objsize, uint64_t align);
void ar_clear(struct arena *a);
void ar_destroy(struct arena *a);
void ar_pop_to(struct arena *a, int64_t want_pos);
void ar_init_fixed(struct arena *a, char *mem, uint64_t size, struct ar_params *params);
void *ar_realloc(struct arena *a, void *ptr, int64_t original_size, int64_t new_size, uint64_t align);

#define arena_init(__a, ...) \
	ar_init(__a, &(struct ar_params){ .source_file = __FILE__, .source_line = __LINE__, __VA_ARGS__ });

#define ar_maken(__a, __type, __n) (__type *)ar_alloc(__a, __n, sizeof(__type), ar_alignof(__type))
#define ar_make(__a, __type) ar_maken(__a, __type, 1)

#define ar_scratch_begin(__a) int64_t ar_scratch_pos = (__a)->pos;
#define ar_scratch_end(__a) ar_pop_to(__a, ar_scratch_pos);

#endif

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "datastructures/arr.h"
#include "log.h"
#include "platform/assert.h"
#include "tracy.h"

void
arr_init_(struct arena *a, struct arr *arr, uint32_t initial, uint32_t item_size, uint32_t item_align)
{
	TracyCZoneAutoS;
	assert(item_size > 0);
	*arr = (struct arr){
		.item_size = item_size,
		.item_align = item_align,
		.cap = initial,
		.e = ar_alloc(a, initial, item_size, item_align),
	};
	TracyCZoneAutoE;
}

void
arr_clear(struct arr *arr)
{
	arr->len = 0;
}

static uint8_t *
arr_point_at(const struct arr *arr, uint32_t i)
{
	return arr->e + (i * arr->item_size);
}

static void *
arr_get_mem(struct arena *a, struct arr *arr)
{
	uint32_t i, newcap;
	++arr->len;
	if (arr->len > arr->cap) {
		assert(arr->cap);
		newcap = arr->cap * 2;
		if (newcap < arr->len) {
			newcap = arr->len * 2;
		}

		arr->e = ar_realloc(a, arr->e, arr->cap * arr->item_size, newcap * arr->item_size, arr->item_align);

		arr->cap = newcap;
	} else {
		/* NOTE: uncomment the below line to cause a realloc for
		 * _every_ push into a arr.  This can help find bugs where you
		 * held a pointer into a arr too long. */
		/* arr->e = z_realloc(arr->e, arr->cap * arr->item_size); */
	}

	i = arr->len - 1;

	return arr_point_at(arr, i);
}

void
arr_grow_by(struct arena *a, struct arr *arr, uint32_t size)
{
	arr->len += size - 1;
	arr_get_mem(a, arr);
}

void
arr_grow_to(struct arena *a, struct arr *arr, uint32_t size)
{
	arr->len = size - 1;
	arr_get_mem(a, arr);
}

uint32_t
arr_push(struct arena *a, struct arr *arr, const void *item)
{
	memcpy(arr_get_mem(a, arr), item, arr->item_size);

	return arr->len - 1;
}

void *
arr_get(const struct arr *arr, uint32_t i)
{
	if (i >= arr->len) {
		L("index %" PRIu64 " out of bounds (%" PRIu64 ")", (uint64_t)i, (uint64_t)arr->len);
	}
	assert(i < arr->len);

	return arr_point_at(arr, i);
}

void
arr_del(struct arr *arr, uint32_t i)
{
	assert(i < arr->len);

	arr->len--;

	if (arr->len > 0 && arr->len != i) {
		memmove(arr_point_at(arr, i), arr_point_at(arr, arr->len), arr->item_size);
	}
}

static struct {
	void *user_ctx;
	sort_func func;
} arr_sort_ctx;

static int32_t
arr_sort_compare(const void *a, const void *b)
{
	return arr_sort_ctx.func(a, b, arr_sort_ctx.user_ctx);
}

void
arr_sort_range(struct arr *arr, uint32_t start, uint32_t end, void *ctx, sort_func func)
{
	arr_sort_ctx.user_ctx = ctx;
	arr_sort_ctx.func = func;

	qsort(arr_point_at(arr, start), end - start, arr->item_size, arr_sort_compare);
}

void
arr_sort(struct arr *arr, void *ctx, sort_func func)
{
	arr_sort_range(arr, 0, arr->len, ctx, func);
}

void *
arr_pop(struct arr *arr)
{
	assert(arr->len);
	--arr->len;
	return arr_point_at(arr, arr->len);
}

void *
arr_peek(struct arr *arr, uint32_t i)
{
	return arr_point_at(arr, arr->len - i);
}

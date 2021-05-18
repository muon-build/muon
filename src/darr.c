#include "posix.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "darr.h"
#include "log.h"
#include "mem.h"

#define DEFAULT_LEN 1024

void
_darr_init(struct darr *darr, size_t item_size)
{
	assert(item_size > 0);
	*darr = (struct darr) { .item_size = item_size };
}

void
darr_destroy(struct darr *da)
{
	if (da->e) {
		z_free(da->e);
	}
}

void
darr_clear(struct darr *da)
{
	da->len = 0;
}

uint8_t *
darr_point_at(const struct darr *da, size_t i)
{
	return da->e + (i * da->item_size);
}

size_t
darr_len(const struct darr *da)
{
	return da->len;
}

size_t
darr_item_size(const struct darr *da)
{
	return da->item_size;
}

size_t
darr_size(const struct darr *da)
{
	return da->item_size * da->len;
}

void *
darr_raw_memory(const struct darr *da)
{
	return da->e;
}

static void *
darr_get_mem(struct darr *da)
{
	size_t i, newcap;
	++da->len;
	/* ensure_mem_size(elem, size, ++(*len), cap); */
	if (da->len > da->cap) {
		if (!da->cap) {
			newcap = da->len > DEFAULT_LEN ? da->len : DEFAULT_LEN;
		} else {
			newcap = da->cap * 2;
		}

#ifndef NDEBUG
		if (!da->secondary) {
			L(log_mem, "%s %ld -> %ld (%s:%d:%s)", da->name, da->cap, newcap, da->file, da->line, da->func);
		}
#endif

		da->cap = newcap;
		da->e = z_realloc(da->e, da->cap * da->item_size);
	}

	i = da->len - 1;

	return darr_point_at(da, i);
}

void
darr_grow_by(struct darr *da, size_t size)
{
	da->len += size - 1;
	darr_get_mem(da);
}

void
darr_grow_to(struct darr *da, size_t size)
{
	if (size > da->len) {
		da->len = size - 1;
		darr_get_mem(da);
	}
}


size_t
darr_push(struct darr *da, const void *item)
{
	memcpy(darr_get_mem(da), item, da->item_size);

	return da->len - 1;
}

void *
darr_try_get(const struct darr *da, size_t i)
{
	if (i < da->len) {
		return darr_point_at(da, i);
	} else {
		return NULL;
	}
}

void *
darr_get(const struct darr *da, size_t i)
{
	if (i >= da->len) {
		L(log_mem, "%s index %ld out of bounds (%ld) (%s:%d:%s)", da->name, i, da->len, da->file, da->line, da->func);
	}
	assert(i < da->len);

	return darr_point_at(da, i);
}

void
darr_set(struct darr *da, size_t i, const void *item)
{
	assert(i < da->len);

	memcpy(darr_point_at(da, i), item, da->item_size);
}

void
darr_del(struct darr *da, size_t i)
{
	assert(i < da->len);

	da->len--;

	if (da->len > 0 && da->len != i) {
		memmove(darr_point_at(da, i), darr_point_at(da, da->len), da->item_size);
	}
}

void
darr_swap(struct darr *da, size_t i, size_t j)
{
	assert(i != j);

	uint8_t tmp[da->item_size];

	void *a = darr_get(da, i),
	     *b = darr_get(da, j);

	memcpy(tmp, a, da->item_size);
	darr_set(da, i, b);
	darr_set(da, j, tmp);
}

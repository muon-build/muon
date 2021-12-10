#include "posix.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "data/darr.h"
#include "log.h"
#include "platform/mem.h"

void
darr_init(struct darr *da, size_t initial, size_t item_size)
{
	assert(item_size > 0);
	*da = (struct darr) {
		.item_size = item_size,
		.cap = initial,
		.e = z_malloc(initial * item_size),
	};
}

void
darr_destroy(struct darr *da)
{
	if (da->e) {
		z_free(da->e);
		da->e = NULL;
	}
}

void
darr_clear(struct darr *da)
{
	da->len = 0;
}

static uint8_t *
darr_point_at(const struct darr *da, size_t i)
{
	return da->e + (i * da->item_size);
}

static void *
darr_get_mem(struct darr *da)
{
	size_t i, newcap;
	++da->len;
	/* ensure_mem_size(elem, size, ++(*len), cap); */
	if (da->len > da->cap) {
		assert(da->cap);
		newcap = da->cap * 2;
		if (newcap < da->len) {
			newcap = da->len * 2;
		}

		da->cap = newcap;
		da->e = z_realloc(da->e, da->cap * da->item_size);
	} else {
		/* NOTE: uncomment the below line to cause a realloc for
		 * _every_ push into a darr.  This can help find bugs where you
		 * held a pointer into a darr too long. */
		/* da->e = z_realloc(da->e, da->cap * da->item_size); */
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

size_t
darr_push(struct darr *da, const void *item)
{
	memcpy(darr_get_mem(da), item, da->item_size);

	return da->len - 1;
}

void *
darr_get(const struct darr *da, size_t i)
{
	if (i >= da->len) {
		L("index %zu out of bounds (%zu)", i, da->len);
	}
	assert(i < da->len);

	return darr_point_at(da, i);
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

static struct {
	void *user_ctx;
	sort_func func;
} darr_sort_ctx;

static int32_t
darr_sort_compare(const void *a, const void *b)
{
	return darr_sort_ctx.func(a, b, darr_sort_ctx.user_ctx);
}

void
darr_sort(struct darr *da, void *ctx, sort_func func)
{
	darr_sort_ctx.user_ctx = ctx;
	darr_sort_ctx.func = func;

	qsort(da->e, da->len, da->item_size, darr_sort_compare);
}

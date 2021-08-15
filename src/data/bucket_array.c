#include "posix.h"

#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "data/bucket_array.h"
#include "log.h"
#include "platform/mem.h"

struct bucket {
	uint8_t *mem;
	uint32_t len;
};

static void
init_bucket(struct bucket_array *ba, struct bucket *b)
{
	b->mem = z_malloc(ba->item_size * ba->bucket_size);
}

void
bucket_array_init(struct bucket_array *ba,
	uint32_t bucket_size, uint32_t item_size)
{
	assert(item_size > 0);

	*ba = (struct bucket_array) {
		.item_size = item_size,
		.bucket_size = bucket_size,
	};

	darr_init(&ba->buckets, 1, item_size);

	darr_push(&ba->buckets, &(struct bucket) { 0 });
	init_bucket(ba, darr_get(&ba->buckets, 0));
}

void *
bucket_array_push(struct bucket_array *ba, const void *item)
{
	void *dest;
	struct bucket *b;

	b = darr_get(&ba->buckets, ba->buckets.len - 1);
	if (b->len >= ba->bucket_size) {
		darr_push(&ba->buckets, &(struct bucket) { 0 });
		b = darr_get(&ba->buckets, ba->buckets.len - 1);
		init_bucket(ba, b);
	}

	dest = b->mem + (b->len * ba->item_size);
	memcpy(dest, item, ba->item_size);
	++b->len;
	++ba->len;

	return dest;
}

void *
bucket_array_get(const struct bucket_array *ba, uint32_t i)
{
	struct bucket *b;
	uint32_t bucket_i = i % ba->bucket_size;

	b = darr_get(&ba->buckets, i / ba->bucket_size);
	assert(bucket_i < b->len);

	return b->mem + (bucket_i * ba->item_size);
}

void
bucket_array_destroy(struct bucket_array *ba)
{
	uint32_t i;

	struct bucket *b;

	for (i = 0; i < ba->buckets.len; ++i) {
		b = darr_get(&ba->buckets, i);

		z_free(b->mem);
	}

	darr_destroy(&ba->buckets);
}

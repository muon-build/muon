#ifndef BUCKET_ARRAY_H
#define BUCKET_ARRAY_H

#include "darr.h"

struct bucket_array {
	struct darr buckets;
	uint32_t item_size;
	uint32_t bucket_size;
	uint32_t len;
};

void bucket_array_init(struct bucket_array *ba, uint32_t bucket_size, uint32_t item_size);
void *bucket_array_push(struct bucket_array *ba, const void *item);
void *bucket_array_get(const struct bucket_array *ba, uint32_t i);
void bucket_array_destroy(struct bucket_array *ba);
#endif

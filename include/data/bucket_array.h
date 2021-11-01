#ifndef MUON_DATA_BUCKET_ARRAY_H
#define MUON_DATA_BUCKET_ARRAY_H

#include "darr.h"

struct bucket_array {
	struct darr buckets;
	uint32_t item_size;
	uint32_t bucket_size;
	uint32_t len;
};

struct bucket {
	uint8_t *mem;
	uint32_t len;
};

void init_bucket(struct bucket_array *ba, struct bucket *b);

void bucket_array_init(struct bucket_array *ba, uint32_t bucket_size, uint32_t item_size);
void *bucket_array_push(struct bucket_array *ba, const void *item);
void *bucket_array_pushn(struct bucket_array *ba, const void *data, uint32_t len, uint32_t reserve);
void *bucket_array_get(const struct bucket_array *ba, uint32_t i);
void bucket_array_clear(struct bucket_array *ba);
void bucket_array_destroy(struct bucket_array *ba);
bool bucket_array_lookup_pointer(struct bucket_array *ba, const uint8_t *p, uint32_t *ret);
#endif

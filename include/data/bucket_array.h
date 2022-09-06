#ifndef MUON_DATA_BUCKET_ARRAY_H
#define MUON_DATA_BUCKET_ARRAY_H

#include "darr.h"

struct bucket_array_save {
	uint32_t tail_bucket, tail_bucket_len;
};

struct bucket_array {
	struct darr buckets;
	uint32_t item_size;
	uint32_t bucket_size;
	uint32_t len, tail_bucket;
};

struct bucket {
	uint8_t *mem;
	uint32_t len;
};

void init_bucket(struct bucket_array *ba, struct bucket *b);
uint64_t bucket_array_size(struct bucket_array *ba);

void bucket_array_init(struct bucket_array *ba, uint32_t bucket_size, uint32_t item_size);
void *bucket_array_push(struct bucket_array *ba, const void *item);
void *bucket_array_pushn(struct bucket_array *ba, const void *data, uint32_t data_len, uint32_t reserve);
void *bucket_array_get(const struct bucket_array *ba, uint32_t i);
void bucket_array_clear(struct bucket_array *ba);
void bucket_array_save(const struct bucket_array *ba, struct bucket_array_save *save);
void bucket_array_restore(struct bucket_array *ba, const struct bucket_array_save *save);
void bucket_array_destroy(struct bucket_array *ba);
bool bucket_array_lookup_pointer(struct bucket_array *ba, const uint8_t *p, uint64_t *ret);
#endif

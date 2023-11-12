/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_DATA_BUCKET_ARR_H
#define MUON_DATA_BUCKET_ARR_H

#include "datastructures/arr.h"

struct bucket_arr_save {
	uint32_t tail_bucket, tail_bucket_len;
};

struct bucket_arr {
	struct arr buckets;
	uint32_t item_size;
	uint32_t bucket_size;
	uint32_t len, tail_bucket;
};

struct bucket {
	uint8_t *mem;
	uint32_t len;
};

void init_bucket(struct bucket_arr *ba, struct bucket *b);
uint64_t bucket_arr_size(struct bucket_arr *ba);

void bucket_arr_init(struct bucket_arr *ba, uint32_t bucket_size, uint32_t item_size);
void *bucket_arr_push(struct bucket_arr *ba, const void *item);
void *bucket_arr_pushn(struct bucket_arr *ba, const void *data, uint32_t data_len, uint32_t reserve);
void *bucket_arr_get(const struct bucket_arr *ba, uint32_t i);
void bucket_arr_clear(struct bucket_arr *ba);
void bucket_arr_save(const struct bucket_arr *ba, struct bucket_arr_save *save);
void bucket_arr_restore(struct bucket_arr *ba, const struct bucket_arr_save *save);
void bucket_arr_destroy(struct bucket_arr *ba);
bool bucket_arr_lookup_pointer(struct bucket_arr *ba, const uint8_t *p, uint64_t *ret);
#endif

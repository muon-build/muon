/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_DATASTRUCTURES_HASH_H
#define MUON_DATASTRUCTURES_HASH_H

#include <stdbool.h>
#include <stdint.h>

#include "datastructures/bucket_arr.h"
#include "datastructures/seg_list.h"
#include "iterator.h"

struct hash;
struct arena;

typedef bool((*hash_keycmp)(const struct hash *h, const void *a, const void *b));
typedef uint64_t((*hash_fn)(const struct hash *h, const void *k));

struct hash {
	struct slist meta, elems;
	struct bucket_arr keys, vals;
	uint32_t cap, load;
	uint32_t key_size, key_align;
	hash_keycmp keycmp;
	hash_fn hash_func;
};

typedef enum iteration_result((*hash_with_keys_iterator_func)(void *ctx, const void *key, uint64_t val));

void hash_init_(struct arena *a, struct hash *h, uint32_t cap, uint32_t key_size, uint32_t key_align);
#define hash_init(__a, __h, __cap, __key_type) \
	hash_init_(__a, __h, __cap, sizeof(__key_type), ar_alignof(__key_type))
void hash_init_str(struct arena *a, struct hash *h, uint32_t cap);

uint64_t *hash_get(const struct hash *h, const void *key);
uint64_t *hash_get_strn(const struct hash *h, const char *str, uint64_t len);
void hash_set(struct arena *a, struct hash *h, const void *key, uint64_t val);
void hash_set_strn(struct arena *a, struct hash *h, const char *s, uint64_t len, uint64_t val);
bool hash_unset(struct hash *h, const void *key);
bool hash_unset_strn(struct hash *h, const char *s, uint64_t len);
#endif

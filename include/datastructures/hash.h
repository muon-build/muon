/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_DATA_HASH_H
#define MUON_DATA_HASH_H

#include "datastructures/arr.h"
#include "iterator.h"

struct hash;

typedef bool((*hash_keycmp)(const struct hash *h, const void *a, const void *b));
typedef uint64_t((*hash_fn)(const struct hash *h, const void *k));

struct hash {
	struct arr meta, e, keys;
	uint32_t cap, len, load, max_load, capm;
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
void hash_set_strn(struct arena *a, struct hash *h, const char *key, uint64_t len, uint64_t val);
void hash_unset(struct hash *h, const void *key);
void hash_unset_strn(struct hash *h, const char *s, uint64_t len);
void hash_clear(struct hash *h);
#endif

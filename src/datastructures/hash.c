/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "datastructures/hash.h"
#include "datastructures/seg_list.h"
#include "platform/assert.h"
#include "tracy.h"
#include "util.h"

#define k_empty 0x80 // 0b10000000
#define k_deleted 0xfe // 0b11111110
#define k_full(v) !((v) & (1 << 7)) // k_full = 0b0xxxxxxx

#define ASSERT_VALID_CAP(cap) assert(cap >= 8 && IS_POWER_OF_TWO(cap));

static const double hash_max_load = 0.7;

struct strkey {
	const char *str;
	uint64_t len;
};

static uint64_t
fnv_1a_64_str(const struct hash *hash, const void *_key)
{
	const struct strkey *key = _key;
	uint64_t h = 14695981039346656037u;
	uint16_t i;

	for (i = 0; i < key->len; i++) {
		h ^= key->str[i];
		h *= 1099511628211u;
	}

	return h;
}

static uint64_t
fnv_1a_64(const struct hash *hash, const void *_key)
{
	const uint8_t *key = _key;
	uint64_t h = 14695981039346656037u;
	uint16_t i;

	for (i = 0; i < hash->key_size; i++) {
		h ^= key[i];
		h *= 1099511628211u;
	}

	return h;
}

static void
fill_meta_with_empty(struct hash *h)
{
	sl_memset(&h->meta, k_empty);

	// const uint32_t len = h->cap >> 3;
	// uint64_t *e = (uint64_t *)h->meta->e;

	// uint32_t i;
	// for (i = 0; i < len; ++i) {
	// 	e[i] = 9259542123273814144u;
	// 	/* this number is just k_empty (128) 8 times:
	// 	 * ((128 << 56) | (128 << 48) | (128 << 40) | (128 << 32)
	// 	 * | (128 << 24) | (128 << 16) | (128 << 8) | (128)) */
	// }
}

static void
prepare_table(struct arena *a, struct hash *h)
{
	sl_clear(&h->meta);
	sl_grow_to(a, &h->meta, h->cap, uint8_t);
	sl_clear(&h->elems);
	sl_grow_to(a, &h->elems, h->cap, uint32_t);

	fill_meta_with_empty(h);
}

static bool
hash_keycmp_memcmp(const struct hash *h, const void *a, const void *b)
{
	return memcmp(a, b, h->key_size) == 0;
}

void
hash_init_(struct arena *a, struct hash *h, uint32_t cap, uint32_t key_size, uint32_t key_align)
{
	TracyCZoneAutoS;

	ASSERT_VALID_CAP(cap);

	*h = (struct hash){ .cap = cap, .key_size = key_size, .key_align = key_align };

	prepare_table(a, h);
	bucket_arr_init_(a, &h->keys, 16, key_size, key_align);
	bucket_arr_init(a, &h->vals, 16, uint64_t);

	h->keycmp = hash_keycmp_memcmp;
	h->hash_func = fnv_1a_64;

	TracyCZoneAutoE;
}

static bool
hash_keycmp_strcmp(const struct hash *_h, const void *_a, const void *_b)
{
	const struct strkey *a = _a, *b = _b;
	return a->len == b->len ? strncmp(a->str, b->str, a->len) == 0 : false;
}

void
hash_init_str(struct arena *a, struct hash *h, uint32_t cap)
{
	hash_init(a, h, cap, struct strkey);
	h->keycmp = hash_keycmp_strcmp;
	h->hash_func = fnv_1a_64_str;
}

static void
probe(const struct hash *h, const void *key, uint32_t **ret_he, uint8_t **ret_meta, uint64_t *hv)
{
#define match ((meta & 0x7f) == h2 && h->keycmp(h, bucket_arr_get(&h->keys, *he), key))

	uint32_t *he;
	*hv = h->hash_func(h, key);
	const uint64_t h1 = *hv >> 7, h2 = *hv & 0x7f;
	uint8_t meta;
	uint64_t hvi = h1 & (h->cap - 1);

	meta = *sl_get(&h->meta, hvi, uint8_t);
	he = sl_get(&h->elems, hvi, uint32_t);

	// printf("probed to %d, 0x%02x, %d, %d\n", (int)hvi, meta, (int)he->keyi, (int)he->val);

	while (meta == k_deleted || (k_full(meta) && !match)) {
		hvi = (hvi + 1) & (h->cap - 1);
		meta = *sl_get(&h->meta, hvi, uint8_t);
		he = sl_get(&h->elems, hvi, uint32_t);
		// printf("--> probed to %d, 0x%02x, %d, %d\n", (int)hvi, meta, (int)he->keyi, (int)he->val);
	}

	*ret_meta = sl_get(&h->meta, hvi, uint8_t);
	*ret_he = he;

#undef match
}

static void
hash_resize(struct arena *a, struct hash *h, uint32_t newcap)
{
	ASSERT_VALID_CAP(newcap);

	h->cap = newcap;
	h->load = h->keys.len;

	prepare_table(a, h);

	for (uint64_t i = 0; i < h->keys.len; ++i) {
		void *key = bucket_arr_get(&h->keys, i);

		uint32_t *he;
		uint8_t *meta;
		uint64_t hv;
		probe(h, key, &he, &meta, &hv);

		*he = i;
		*meta = hv & 0x7f;
	}
}

uint64_t *
hash_get(const struct hash *h, const void *key)
{
	uint32_t *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	return k_full(*meta) ? bucket_arr_get(&h->vals, *he) : NULL;
}

uint64_t *
hash_get_strn(const struct hash *h, const char *str, uint64_t len)
{
	struct strkey key = { .str = str, .len = len };
	return hash_get(h, &key);
}

bool
hash_unset(struct hash *h, const void *key)
{
	uint32_t *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	if (k_full(*meta)) {
		*meta = k_deleted;

		if (*he != h->keys.len - 1) {
			uint32_t *tail;
			void *tail_key = bucket_arr_get(&h->keys, h->keys.len - 1);
			probe(h, tail_key, &tail, &meta, &hv );
			*tail = *he;
		}

		bucket_arr_del(&h->keys, *he);
		bucket_arr_del(&h->vals, *he);

		return true;
	}

	return false;
}

bool
hash_unset_strn(struct hash *h, const char *s, uint64_t len)
{
	struct strkey key = { .str = s, .len = len };
	return hash_unset(h, &key);
}

void
hash_set(struct arena *a, struct hash *h, const void *key, uint64_t val)
{
	if (h->load >= (uint32_t)(h->cap * hash_max_load)) {
		hash_resize(a, h, h->cap << 1);
	}

	uint32_t *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	if (k_full(*meta)) {
		*(uint64_t *)bucket_arr_get(&h->vals, *he) = val;
	} else {
		*meta = hv & 0x7f;
		*he = h->keys.len;
		bucket_arr_push(a, &h->keys, key);
		++h->load;
		bucket_arr_push(a, &h->vals, &val);
	}
}

void
hash_set_strn(struct arena *a, struct hash *h, const char *s, uint64_t len, uint64_t val)
{
	struct strkey key = { .str = s, .len = len };
	hash_set(a, h, &key, val);
}

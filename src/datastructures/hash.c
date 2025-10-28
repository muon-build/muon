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

struct hash_elem {
	uint64_t val, keyi;
};

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
	sl_clear(&h->e);
	sl_grow_to(a, &h->e, h->cap, struct hash_elem);

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

void
hash_clear(struct hash *h)
{
	h->len = 0;
	sl_clear(&h->keys);
	fill_meta_with_empty(h);
}

static void
probe(const struct hash *h, const void *key, struct hash_elem **ret_he, uint8_t **ret_meta, uint64_t *hv)
{
#define match ((meta & 0x7f) == h2 && h->keycmp(h, sl_get_(sl_cast(&h->keys), he->keyi, h->key_size), key))

	struct hash_elem *he;
	*hv = h->hash_func(h, key);
	const uint64_t h1 = *hv >> 7, h2 = *hv & 0x7f;
	uint8_t meta;
	uint64_t hvi = h1 & (h->cap - 1);

	meta = *sl_get(sl_cast(&h->meta), hvi, uint8_t);
	he = sl_get(sl_cast(&h->e), hvi, struct hash_elem);

	// printf("probed to %d, 0x%02x, %d, %d\n", (int)hvi, meta, (int)he->keyi, (int)he->val);

	while (meta == k_deleted || (k_full(meta) && !match)) {
		hvi = (hvi + 1) & (h->cap - 1);
		meta = *sl_get(sl_cast(&h->meta), hvi, uint8_t);
		he = sl_get(sl_cast(&h->e), hvi, struct hash_elem);
		// printf("--> probed to %d, 0x%02x, %d, %d\n", (int)hvi, meta, (int)he->keyi, (int)he->val);
	}

	*ret_meta = sl_get(&h->meta, hvi, uint8_t);
	*ret_he = he;

#undef match
}

static void
hash_resize(struct arena *a, struct arena *a_scratch, struct hash *h, uint32_t newcap)
{
	ASSERT_VALID_CAP(newcap);
	assert(h->len <= newcap);

	// uint64_t i, hv;
	// struct hash_elem *ohe, *he;
	// uint8_t *meta;
	// void *key;

	struct slist16 tmp = { 0 };

	// printf("resizing %d -> %d\n", h->cap, newcap);
	for (uint64_t i = 0; i < h->cap; ++i) {
		// printf("> checking %d %02x\n", (int)i, *(uint8_t *)sl_get(h->meta, i));
		if (!k_full(*sl_get(&h->meta, i, uint8_t))) {
			continue;
		}

		struct hash_elem *ohe = sl_get(&h->e, i, struct hash_elem);
		// void *key = sl_get(h->keys, ohe->keyi);
		// printf("> full, ohe: %d, %d, key: %.*s\n",
		// 	(int)ohe->keyi,
		// 	(int)ohe->val,
		// 	(int)((struct strkey *)key)->len,
		// 	((struct strkey *)key)->str);
		sl_push(a_scratch, &tmp, ohe, struct hash_elem);
		if (tmp.len >= h->len) {
			// break early when we know the rest of the elements
			// are empty
			break;
		}
	}

	h->cap = newcap;

	prepare_table(a, h);

	sl_for(&tmp, struct hash_elem) {
		void *key = sl_get_(sl_cast(&h->keys), it.it->keyi, h->key_size);
		// printf("probing %.*s\n", (int)((struct strkey *)key)->len, ((struct strkey *)key)->str);

		struct hash_elem *he;
		uint8_t *meta;
		uint64_t hv;
		probe(h, key, &he, &meta, &hv);

		assert(*meta == k_empty);

		*he = *it.it;
		*meta = hv & 0x7f;

		assert(k_full(*meta));
	}

#if 0
	sl_for(h->meta, uint8_t) {
		bool full = k_full(*it.it);
		struct hash_elem *ohe = (full ? sl_get(h->e, it.idx) : 0);
		struct strkey *key = (full ? sl_get(h->keys, ohe->keyi) : 0);
		printf("  0x%02x, %d %d %.*s %s %c\n",
			*it.it,
			full,
			full ? (int)ohe->keyi : 0,
			key ? (int)key->len : 0,
			key ? key->str : 0,
			full ? "=>" : "",
			full ? (int)ohe->val : 0);
	}
#endif
}

uint64_t *
hash_get(const struct hash *h, const void *key)
{
	struct hash_elem *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	return k_full(*meta) ? &he->val : NULL;
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
	struct hash_elem *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	bool found = false;

	if (k_full(*meta)) {
		found = true;
		*meta = k_deleted;

		/* When a value is unset, delete its corresponding key.  Since the
		 * deletion swaps the deleted key with the key in tail postion, unless
		 * the deleted key is in tail position already, we need to scan the
		 * hash elem array to find the hash element that references the tail
		 * key and fix it's keyi value.
		 */
		const uint64_t old_key_idx = he->keyi;
		*he = (struct hash_elem){ 0 };
		const uint64_t tail_key_idx = h->keys.len - 1;
		assert(h->keys.len == h->len);

		if (tail_key_idx != old_key_idx) {
			for (uint64_t i = 0; i < h->cap; ++i) {
				if (!k_full(*sl_get(&h->meta, i, uint8_t))) {
					continue;
				}

				he = sl_get(&h->e, i, struct hash_elem);
				if (he->keyi == tail_key_idx) {
					break;
				}
			}

			assert(he->keyi == tail_key_idx && "hash elem with tail key index not found?");
			he->keyi = old_key_idx;
		}

		sl_del_(sl_cast(&h->keys), old_key_idx, h->key_size);
		--h->len;
	}

	assert(hash_get(h, key) == NULL);

	return found;
}

bool
hash_unset_strn(struct hash *h, const char *s, uint64_t len)
{
	struct strkey key = { .str = s, .len = len };
	return hash_unset(h, &key);
}

void
hash_set(struct arena *a, struct arena *a_scratch, struct hash *h, const void *key, uint64_t val)
{
	if (h->len >= h->cap >> 1) {
		hash_resize(a, a_scratch, h, h->cap << 1);
	}

	struct hash_elem *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	if (k_full(*meta)) {
		he->val = val;
	} else {
		he->keyi = h->keys.len;
		sl_push_(a, sl_cast(&h->keys), key, h->key_size, h->key_align, ARRAY_LEN(h->keys.segments));
		he->val = val;
		*meta = hv & 0x7f;
		++h->len;
	}
}

void
hash_set_strn(struct arena *a, struct arena *a_scratch, struct hash *h, const char *s, uint64_t len, uint64_t val)
{
	struct strkey key = { .str = s, .len = len };
	hash_set(a, a_scratch, h, &key, val);
}

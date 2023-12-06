/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "datastructures/arr.h"
#include "datastructures/hash.h"
#include "log.h"
#include "platform/mem.h"

#define k_empty    0x80 // 0b10000000
#define k_deleted  0xfe // 0b11111110
#define k_full(v)  !(v & (1 << 7)) // k_full = 0b0xxxxxxx

#define ASSERT_VALID_CAP(cap) assert(cap >= 8); assert((cap & (cap - 1)) == 0);

#define LOAD_FACTOR 0.5f

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

	for (i = 0; i < hash->keys.item_size; i++) {
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
	const uint32_t len = h->cap >> 3;
	uint64_t *e = (uint64_t *)h->meta.e;

	uint32_t i;
	for (i = 0; i < len; ++i) {
		e[i] = 9259542123273814144u;
		/* this number is just k_empty (128) 8 times:
		 * ((128 << 56) | (128 << 48) | (128 << 40) | (128 << 32)
		 * | (128 << 24) | (128 << 16) | (128 << 8) | (128)) */
	}
}

static void
prepare_table(struct hash *h)
{
	fill_meta_with_empty(h);
}

static bool
hash_keycmp_memcmp(const struct hash *h, const void *a, const void *b)
{
	return memcmp(a, b, h->keys.item_size) == 0;
}

void
hash_init(struct hash *h, size_t cap, uint32_t keysize)
{
	ASSERT_VALID_CAP(cap);

	*h = (struct hash) {
		.cap = cap, .capm = cap - 1,
		.max_load = (size_t)((float)cap * LOAD_FACTOR)
	};
	arr_init(&h->meta, h->cap, sizeof(uint8_t));
	arr_init(&h->e, h->cap, sizeof(struct hash_elem));
	arr_init(&h->keys, h->cap, keysize);

	prepare_table(h);

	h->keycmp = hash_keycmp_memcmp;
	h->hash_func = fnv_1a_64;
}

static bool
hash_keycmp_strcmp(const struct hash *_h, const void *_a, const void *_b)
{
	const struct strkey *a = _a, *b = _b;
	return a->len == b->len ? strncmp(a->str, b->str, a->len) == 0 : false;
}

void
hash_init_str(struct hash *h, size_t cap)
{
	hash_init(h, cap, sizeof(struct strkey));
	h->keycmp = hash_keycmp_strcmp;
	h->hash_func = fnv_1a_64_str;
}

void
hash_destroy(struct hash *h)
{
	arr_destroy(&h->meta);
	arr_destroy(&h->e);
	arr_destroy(&h->keys);
}

void
hash_for_each(struct hash *h, void *ctx, iterator_func ifnc)
{
	size_t i;

	for (i = 0; i < h->cap; ++i) {
		if (!k_full(((uint8_t *)h->meta.e)[i])) {
			continue;
		}

		switch (ifnc(ctx, &((struct hash_elem *)h->e.e)[i].val)) {
		case ir_cont:
			break;
		case ir_done:
		case ir_err:
			return;
		}
	}
}

void
hash_for_each_with_keys(struct hash *h, void *ctx, hash_with_keys_iterator_func ifnc)
{
	size_t i;
	struct hash_elem *he;

	for (i = 0; i < h->cap; ++i) {
		if (!k_full(((uint8_t *)h->meta.e)[i])) {
			continue;
		}

		he = &((struct hash_elem *)h->e.e)[i];

		switch (ifnc(ctx, h->keys.e + he->keyi * h->keys.item_size, he->val)) {
		case ir_cont:
			break;
		case ir_done:
		case ir_err:
			return;
		}
	}
}

void
hash_clear(struct hash *h)
{
	h->len = h->load = 0;
	fill_meta_with_empty(h);
}

static void
probe(const struct hash *h, const void *key, struct hash_elem **ret_he, uint8_t **ret_meta, uint64_t *hv)
{
#define match ((meta & 0x7f) == h2 \
	       && h->keycmp(h, h->keys.e + (h->keys.item_size * he->keyi), key))

	struct hash_elem *he;
	*hv = h->hash_func(h, key);
	const uint64_t h1 = *hv >> 7, h2 = *hv & 0x7f;
	uint8_t meta;
	uint64_t hvi = h1 & h->capm;

	meta = ((uint8_t *)h->meta.e)[hvi];
	he = &((struct hash_elem *)h->e.e)[hvi];

	while (meta == k_deleted || (k_full(meta) && !match)) {
		hvi = (hvi + 1) & h->capm;
		meta = ((uint8_t *)h->meta.e)[hvi];
		he = &((struct hash_elem *)h->e.e)[hvi];
	}

	*ret_meta = &((uint8_t *)h->meta.e)[hvi];
	*ret_he = he;

#undef match
}

static void
resize(struct hash *h, size_t newcap)
{
	ASSERT_VALID_CAP(newcap);
	assert(h->len <= newcap);

	uint32_t i;
	struct hash_elem *ohe, *he;
	uint64_t hv;
	uint8_t *meta;
	void *key;

	struct hash newh = (struct hash) {
		.cap = newcap, .capm = newcap - 1, .keys = h->keys,
		.len = h->len, .load = h->load,
		.max_load = (size_t)((float)newcap * LOAD_FACTOR),

		.hash_func = h->hash_func,
		.keycmp = h->keycmp,
	};

	arr_init(&newh.meta, newh.cap, sizeof(uint8_t));
	arr_init(&newh.e, newh.cap, sizeof(struct hash_elem));

	prepare_table(&newh);

	for (i = 0; i < h->cap; ++i) {
		if (!k_full(((uint8_t *)h->meta.e)[i])) {
			continue;
		}

		ohe = &((struct hash_elem *)h->e.e)[i];
		key = h->keys.e + (h->keys.item_size * ohe->keyi);

		probe(&newh, key, &he, &meta, &hv);

		assert(!k_full(*meta));

		*he = *ohe;
		*meta = hv & 0x7f;
	}

	arr_destroy(&h->meta);
	arr_destroy(&h->e);
	*h = newh;
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

void
hash_unset(struct hash *h, const void *key)
{
	struct hash_elem *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	if (k_full(*meta)) {
		*meta = k_deleted;
		--h->len;
	}

	assert(hash_get(h, key) == NULL);
}

void
hash_unset_str(struct hash *h, const char *key)
{
	hash_unset(h, &key);
}

void
hash_set(struct hash *h, const void *key, uint64_t val)
{
	if (h->load > h->max_load) {
		resize(h, h->cap << 1);
	}

	struct hash_elem *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	if (k_full(*meta)) {
		he->val = val;
	} else {
		he->keyi = arr_push(&h->keys, key);
		he->val = val;
		*meta = hv & 0x7f;
		++h->len;
		++h->load;
	}
}

void
hash_set_strn(struct hash *h, const char *s, uint64_t len, uint64_t val)
{
	struct strkey key = { .str = s, .len = len };
	hash_set(h, &key, val);
}

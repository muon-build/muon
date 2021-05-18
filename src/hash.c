#include "posix.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "darr.h"
#include "hash.h"
#include "log.h"
#include "mem.h"

#define k_empty    0x80 // 0b10000000
#define k_deleted  0xfe // 0b11111110
#define k_full(v)  !(v & (1 << 7)) // k_full = 0b0xxxxxxx

#define ASSERT_VALID_CAP(cap) assert(cap >= 8); assert((cap & (cap - 1)) == 0);

#define HASH_FUNC fnv_1a_64

#define LOAD_FACTOR 0.5f

static uint64_t
fnv_1a_64(const char *key)
{
	uint64_t h = 14695981039346656037u;
	uint16_t i;

	for (i = 0; i < key[i]; i++) {
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
	darr_grow_to(&h->meta, h->cap);
	darr_grow_to(&h->e, h->cap);

	fill_meta_with_empty(h);
}

void
_hash_init(struct hash *h, size_t cap)
{
	ASSERT_VALID_CAP(cap);

	*h = (struct hash) {
		.cap = cap, .capm = cap - 1,
		.max_load = (size_t)((float)cap * LOAD_FACTOR)
	};
	darr_init(&h->meta, sizeof(uint8_t));
	darr_init(&h->e, sizeof(struct hash_elem));
	darr_init(&h->keys, sizeof(const char *));

	prepare_table(h);
}

void
hash_destroy(struct hash *h)
{
	darr_destroy(&h->meta);
	darr_destroy(&h->e);
	darr_destroy(&h->keys);
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

		switch (ifnc(ctx, (char *)(h->keys.e + he->keyi * h->keys.item_size), he->val)) {
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
	/* maybe only this is necessarry? */
	fill_meta_with_empty(h);
}

#define match ((meta & 0x7f) == h2 \
	       && strcmp(*(char **)(h->keys.e + (h->keys.item_size * he->keyi)), key) == 0)

static void
probe(const struct hash *h, const char *key, struct hash_elem **ret_he, uint8_t **ret_meta, uint64_t *hv)
{
	struct hash_elem *he;
	*hv = HASH_FUNC(key);
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
}

static void
resize(struct hash *h, size_t newcap)
{
	ASSERT_VALID_CAP(newcap);
	assert(h->len <= newcap);

#ifndef NDEBUG
	L(log_mem, "%s %ld -> %ld (%s:%d:%s)", h->name, h->cap, newcap, h->file, h->line, h->func);
#endif

	uint32_t i;
	struct hash_elem *ohe, *he;
	uint64_t hv;
	uint8_t *meta;
	void *key;

	struct hash newh = (struct hash) {
		.cap = newcap, .capm = newcap - 1, .keys = h->keys,
		.len = h->len, .load = h->load,
		.max_load = (size_t)((float)newcap * LOAD_FACTOR),
	};

	darr_init(&newh.meta, sizeof(uint8_t));
	darr_init(&newh.e, sizeof(struct hash_elem));

#ifndef NDEBUG
	newh.name = h->name;
	newh.file = h->file;
	newh.func = h->func;
	newh.line = h->line;
	newh.secondary = h->secondary;

	newh.meta.name = h->meta.name;
	newh.meta.func = h->meta.func;
	newh.meta.file = h->meta.file;
	newh.meta.line = h->meta.line;
	newh.meta.secondary = h->meta.secondary;

	newh.e.name = h->meta.name;
	newh.e.func = h->meta.func;
	newh.e.file = h->meta.file;
	newh.e.line = h->meta.line;
	newh.e.secondary = h->meta.secondary;
#endif

	prepare_table(&newh);

	for (i = 0; i < h->cap; ++i) {
		if (!k_full(((uint8_t *)h->meta.e)[i])) {
			continue;
		}

		ohe = &((struct hash_elem *)h->e.e)[i];
		key = h->keys.e + (ohe->keyi * h->keys.item_size);

		probe(&newh, key, &he, &meta, &hv);

		assert(!k_full(*meta));

		*he = *ohe;
		*meta = hv & 0x7f;
	}

	darr_destroy(&h->meta);
	darr_destroy(&h->e);
	*h = newh;
}

uint64_t *
hash_get(const struct hash *h, const char *key)
{
	struct hash_elem *he;
	uint64_t hv;
	uint8_t *meta;

	probe(h, key, &he, &meta, &hv);

	return k_full(*meta) ? &he->val : NULL;
}

void
hash_unset(struct hash *h, const char *key)
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
hash_set(struct hash *h, const char *key, uint64_t val)
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
		he->keyi = darr_push(&h->keys, &key);
		he->val = val;
		*meta = hv & 0x7f;
		++h->len;
		++h->load;
	}
}

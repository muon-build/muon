/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "external/samurai/ctx.h"

#include "external/samurai/htab.h"
#include "external/samurai/util.h"

struct samu_hashtable {
	size_t len, cap;
	struct samu_hashtablekey *keys;
	void **vals;
};

void
samu_htabkey(struct samu_hashtablekey *k, const char *s, size_t n)
{
	k->str = s;
	k->len = n;
	k->hash = samu_murmurhash64a(s, n);
}

struct samu_hashtable *
samu_mkhtab(struct samu_arena *a, size_t cap)
{
	struct samu_hashtable *h;
	size_t i;

	assert(!(cap & (cap - 1)));
	h = samu_xmalloc(a, sizeof(*h));
	h->len = 0;
	h->cap = cap;
	h->keys = samu_xreallocarray(a, NULL, 0, cap, sizeof(h->keys[0]));
	h->vals = samu_xreallocarray(a, NULL, 0, cap, sizeof(h->vals[0]));
	for (i = 0; i < cap; ++i)
		h->keys[i].str = NULL;

	return h;
}

static bool
samu_keyequal(struct samu_hashtablekey *k1, struct samu_hashtablekey *k2)
{
	if (k1->hash != k2->hash || k1->len != k2->len)
		return false;
	return memcmp(k1->str, k2->str, k1->len) == 0;
}

static size_t
samu_keyindex(struct samu_hashtable *h, struct samu_hashtablekey *k)
{
	size_t i;

	i = k->hash & (h->cap - 1);
	while (h->keys[i].str && !samu_keyequal(&h->keys[i], k))
		i = (i + 1) & (h->cap - 1);
	return i;
}

void **
samu_htabput(struct samu_arena *a, struct samu_hashtable *h, struct samu_hashtablekey *k)
{
	struct samu_hashtablekey *oldkeys;
	void **oldvals;
	size_t i, j, oldcap;

	if (h->cap / 2 < h->len) {
		oldkeys = h->keys;
		oldvals = h->vals;
		oldcap = h->cap;
		h->cap *= 2;
		h->keys = samu_xreallocarray(a, NULL, 0, h->cap, sizeof(h->keys[0]));
		h->vals = samu_xreallocarray(a, NULL, 0, h->cap, sizeof(h->vals[0]));
		for (i = 0; i < h->cap; ++i)
			h->keys[i].str = NULL;
		for (i = 0; i < oldcap; ++i) {
			if (oldkeys[i].str) {
				j = samu_keyindex(h, &oldkeys[i]);
				h->keys[j] = oldkeys[i];
				h->vals[j] = oldvals[i];
			}
		}
	}
	i = samu_keyindex(h, k);
	if (!h->keys[i].str) {
		h->keys[i] = *k;
		h->vals[i] = NULL;
		++h->len;
	}

	return &h->vals[i];
}

void *
samu_htabget(struct samu_hashtable *h, struct samu_hashtablekey *k)
{
	size_t i;

	i = samu_keyindex(h, k);
	return h->keys[i].str ? h->vals[i] : NULL;
}

uint64_t
samu_murmurhash64a(const void *ptr, size_t len)
{
	const uint64_t seed = 0xdecafbaddecafbadull;
	const uint64_t m = 0xc6a4a7935bd1e995ull;
	uint64_t h, k, n;
	const uint8_t *p, *end;
	int r = 47;

	h = seed ^ (len * m);
	n = len & ~0x7ull;
	end = ptr;
	end += n;
	for (p = ptr; p != end; p += 8) {
		memcpy(&k, p, sizeof(k));

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	switch (len & 0x7) {
	case 7: h ^= (uint64_t)p[6] << 48;  /* fallthrough */
	case 6: h ^= (uint64_t)p[5] << 40;  /* fallthrough */
	case 5: h ^= (uint64_t)p[4] << 32;  /* fallthrough */
	case 4: h ^= (uint64_t)p[3] << 24;  /* fallthrough */
	case 3: h ^= (uint64_t)p[2] << 16;  /* fallthrough */
	case 2: h ^= (uint64_t)p[1] <<  8;  /* fallthrough */
	case 1: h ^= (uint64_t)p[0];
		h *= m;
	}

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}

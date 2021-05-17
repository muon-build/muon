#include "posix.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "darr.h"
#include "hash.h"
#include "hdarr.h"
#include "log.h"
#include "mem.h"

void
_hdarr_init(struct hdarr *hd, size_t size, size_t keysize, size_t item_size, hdarr_key_getter kg)
{
	darr_init(&hd->darr, item_size);
	hash_init(&hd->hash, size, keysize);
	hd->kg = kg;
}

void
hdarr_destroy(struct hdarr *hd)
{
	darr_destroy(&hd->darr);
	hash_destroy(&hd->hash);
}

void *
hdarr_get(const struct hdarr *hd, const void *key)
{
	const uint64_t *val;

	if ((val = hash_get(&hd->hash, key)) == NULL) {
		return NULL;
	} else {
		return darr_get(&hd->darr, *val);
	}
}

/* TODO: should hdarr_get_i and get_by_i be deprecated ? */

const uint64_t *
hdarr_get_i(struct hdarr *hd, const void *key)
{
	return hash_get(&hd->hash, key);
}

void *
hdarr_get_by_i(struct hdarr *hd, size_t i)
{
	return darr_get(&hd->darr, i);
}

void
hdarr_del(struct hdarr *hd, const void *key)
{
	const uint64_t *val;
	size_t len;
	const void *tailkey;

	if ((val = hash_get(&hd->hash, key)) == NULL) {
		return;
	} else {
		hash_unset(&hd->hash, key);

		darr_del(&hd->darr, *val);

		if ((len = darr_len(&hd->darr)) > 0 && len != *val) {
			tailkey = hd->kg(darr_get(&hd->darr, *val));
			hash_set(&hd->hash, tailkey, *val);
		}
	}
}

size_t
hdarr_set(struct hdarr *hd, const void *key, const void *value)
{
	size_t i;
	const uint64_t *val;

	if ((val = hash_get(&hd->hash, key)) == NULL) {
		i = darr_push(&hd->darr, value);

		hash_set(&hd->hash, key, i);
		return i;
	} else {
		darr_set(&hd->darr, *val, value);
		return *val;
	}
}

void
hdarr_clear(struct hdarr *hd)
{
	hash_clear(&hd->hash);
	darr_clear(&hd->darr);
}

size_t
hdarr_len(const struct hdarr *hd)
{
	return darr_len(&hd->darr);
}

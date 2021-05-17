#ifndef UTIL_HDARR_H
#define UTIL_HDARR_H

#include <stdbool.h>
#include <stddef.h>

#include "darr.h"
#include "hash.h"
#include "iterator.h"

typedef const void *(*hdarr_key_getter)(void *elem);

struct hdarr {
	struct hash hash;
	struct darr darr;

	hdarr_key_getter kg;

#ifndef NDEBUG
	const char *name;
#endif
};

#ifndef NDEBUG
#define hdarr_init(hd, size, keysize, item_size, kg) \
	do { \
		_hdarr_init(hd, size, keysize, item_size, kg); \
		(hd)->darr.name = #hd ".darr"; \
		(hd)->darr.func = __func__; \
		(hd)->darr.file = __FILE__; \
		(hd)->darr.line = __LINE__; \
		(hd)->darr.secondary = true; \
		(hd)->hash.name = #hd ".hash"; \
		(hd)->hash.func = __func__; \
		(hd)->hash.file = __FILE__; \
		(hd)->hash.line = __LINE__; \
		(hd)->hash.secondary = true; \
		(hd)->hash.meta.name = #hd ".hash.meta"; \
		(hd)->hash.meta.func = __func__; \
		(hd)->hash.meta.file = __FILE__; \
		(hd)->hash.meta.line = __LINE__; \
		(hd)->hash.e.name = #hd ".hash.e"; \
		(hd)->hash.e.func = __func__; \
		(hd)->hash.e.file = __FILE__; \
		(hd)->hash.e.line = __LINE__; \
		(hd)->hash.keys.name = #hd ".hash.keys"; \
		(hd)->hash.keys.func = __func__; \
		(hd)->hash.keys.file = __FILE__; \
		(hd)->hash.keys.line = __LINE__; \
	} while (0)
#else
#define hdarr_init(hd, size, keysize, item_size, kg) _hdarr_init(hd, size, keysize, item_size, kg)
#endif

void _hdarr_init(struct hdarr *hd, size_t size, size_t keysize, size_t item_size, hdarr_key_getter kg);
void hdarr_destroy(struct hdarr *hd);

void hdarr_del(struct hdarr *hd, const void *key);
void *hdarr_get(const struct hdarr *hd, const void *key);
const uint64_t *hdarr_get_i(struct hdarr *hd, const void *key);
void *hdarr_get_by_i(struct hdarr *hd, size_t i);
size_t hdarr_set(struct hdarr *hd, const void *key, const void *value);
size_t hdarr_len(const struct hdarr *hd);
void hdarr_clear(struct hdarr *hd);
#endif

#ifndef TYPES_HASH_H
#define TYPES_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "darr.h"
#include "iterator.h"

struct hash {
	struct darr meta, e, keys;
	size_t cap, len, load, max_load, capm;
#ifndef NDEBUG
	const char *name;
	const char *file;
	const char *func;
	uint32_t line;
	bool secondary;
#endif
};

typedef enum iteration_result ((*hash_with_keys_iterator_func)(void *ctx, const char *key, uint64_t val));


#ifndef NDEBUG
#define hash_init(h, cap) \
	do { \
		_hash_init(h, cap); \
		(h)->func = __func__; \
		(h)->file = __FILE__; \
		(h)->line = __LINE__; \
		(h)->meta.name = #h ".meta"; \
		(h)->meta.func = __func__; \
		(h)->meta.file = __FILE__; \
		(h)->meta.line = __LINE__; \
		(h)->meta.secondary = true; \
		(h)->e.name = #h ".e"; \
		(h)->e.func = __func__; \
		(h)->e.file = __FILE__; \
		(h)->e.line = __LINE__; \
		(h)->e.secondary = true; \
		(h)->keys.name = #h ".keys"; \
		(h)->keys.func = __func__; \
		(h)->keys.file = __FILE__; \
		(h)->keys.line = __LINE__; \
		(h)->keys.secondary = true; \
	} while (0)
#else
#define hash_init(h, cap) _hash_init(h, cap)
#endif

void _hash_init(struct hash *h, size_t cap);
void hash_destroy(struct hash *h);

uint64_t *hash_get(const struct hash *h, const char *key);
void hash_set(struct hash *h, const char *key, uint64_t val);
void hash_unset(struct hash *h, const char *key);
void hash_clear(struct hash *h);

void hash_for_each(struct hash *h, void *ctx, iterator_func ifnc);
void hash_for_each_with_keys(struct hash *h, void *ctx, hash_with_keys_iterator_func ifnc);
#endif

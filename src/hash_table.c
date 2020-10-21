#include "hash_table.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static uint64_t
murmur_hash(void *ptr, size_t size)
{
	const uint64_t seed = 0xdeadbabebaddecafull;
	const uint64_t m = 0xc6a4a7935bd1e995ull;
	uint64_t h, k, n;
	const uint8_t *p, *end;
	int r = 47;

	h = seed ^ (size * m);
	n = size & ~0x7ull;
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

	switch (size & 0x7) {
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

struct hash_table *
hash_table_create(size_t capacity)
{
	struct hash_table *table = calloc(1, sizeof(struct hash_table));
	assert(table);

	table->size = 0;
	table->capacity = capacity;

	table->keys = calloc(capacity, sizeof(struct hash_key));
	table->values = calloc(capacity, sizeof(void*));

	return table;
}

static bool
key_equal(struct hash_key *key, struct hash_key *other)
{
	if (key->hash != other->hash && key->size != other->size) {
		return false;
	}

	return memcmp(key->data, other->data, key->size) == 0;
}

static size_t
key_index(struct hash_table *table, struct hash_key *key)
{
	size_t i = key->hash & (table->capacity - 1);
	while (table->keys[i].data && !key_equal(&table->keys[i], key)) {
		i = (i + 1) & (table->capacity - 1);
	}

	return i;
}

void **
hash_table_put(struct hash_table *table, const char *data)
{
	struct hash_key key = {
		.hash = murmur_hash((void *)data, strlen(data)),
		.data = data,
		.size = strlen(data),
	};

	if (table->capacity / 2 < table->size) {
		struct hash_key *old_keys = table->keys;
		void **old_values = table->values;
		size_t old_capacity = table->capacity;

		table->capacity *= 2;

		table->keys = calloc(table->capacity, sizeof(table->keys[0]));
		table->values = calloc(table->capacity, sizeof(table->values[0]));

		for (size_t i = 0; i < old_capacity; ++i) {
			if (old_keys[i].data) {
				const size_t j = key_index(table, &old_keys[i]);
				table->keys[j] = old_keys[i];
				table->values[j] = old_values[i];
			}
		}
		free(old_keys);
		free(old_values);
	}

	const size_t i = key_index(table, &key);

	if (!table->keys[i].data) {
		table->keys[i] = key;
		table->values[i] = NULL;
		++table->size;
	}

	return &table->values[i];
}

void *
hash_table_get(struct hash_table *table, const char *data)
{
	struct hash_key key = {
		.hash = murmur_hash((void*)data, strlen(data)),
		.data = data,
		.size = strlen(data),
	};

	const size_t i = key_index(table, &key);

	return table->keys[i].data ? table->values[i] : NULL;
}

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stdint.h>
#include <stddef.h>

struct hash_key {
	uint64_t hash;
	const char *data;
	size_t size;
};

struct hash_table {
	uint32_t size;
	uint32_t capacity;
	struct hash_key *keys;
	void **values;
};

struct hash_table *hash_table_create(size_t);

void **hash_table_put(struct hash_table *, const char *);
void *hash_table_get(struct hash_table *, const char *);

#endif // HASH_TABLE_H

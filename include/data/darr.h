#ifndef SHARED_TYPES_DARR_H
#define SHARED_TYPES_DARR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct darr {
	size_t len;
	size_t cap;
	size_t item_size;
	uint8_t *e;
};

void darr_init(struct darr *darr, size_t initial, size_t item_size);
void darr_destroy(struct darr *da);
size_t darr_push(struct darr *da, const void *item);
void *darr_get(const struct darr *da, size_t i);
void darr_del(struct darr *da, size_t i);
void darr_clear(struct darr *da);
void darr_grow_by(struct darr *da, size_t size);
#endif

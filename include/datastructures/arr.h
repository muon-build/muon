/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_DATA_ARR_H
#define MUON_DATA_ARR_H

#include <stdbool.h>
#include <stdint.h>

#include "arena.h"

struct arr {
	uint32_t len;
	uint32_t cap;
	uint32_t item_size, item_align;
	uint8_t *e;
};

void arr_init_(struct arena *a, struct arr *arr, uint32_t initial, uint32_t item_size, uint32_t item_align);
#define arr_init(__a, __arr, __initial, __type) \
	arr_init_(__a, __arr, __initial, sizeof(__type), ar_alignof(__type))
uint32_t arr_push(struct arena *a, struct arr *arr, const void *item);
void *arr_pop(struct arr *arr);
void *arr_peek(struct arr *arr, uint32_t i);
void *arr_get(const struct arr *arr, uint32_t i);
void arr_del(struct arr *arr, uint32_t i);
void arr_clear(struct arr *arr);
void arr_grow_by(struct arena *a, struct arr *arr, uint32_t size);
void arr_grow_to(struct arena *a, struct arr *arr, uint32_t size);

typedef int32_t (*sort_func)(const void *a, const void *b, void *ctx);
void arr_sort_range(struct arr *arr, uint32_t start, uint32_t end, void *ctx, sort_func func);
void arr_sort(struct arr *arr, void *ctx, sort_func func);
#endif

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef LANG_OBJECT_ITERATORS_H
#define LANG_OBJECT_ITERATORS_H

#include <stdbool.h>

#include "lang/types.h"
#include "preprocessor_helpers.h"

/******************************************************************************
 * obj_array_for
 ******************************************************************************/

struct obj_array_for_helper {
	const struct obj_array *a;
	struct obj_array_elem *e;
	uint32_t i, len;
};

#define obj_array_for_array_(__wk, __arr, __val, __iter)                                        \
	struct obj_array_for_helper __iter = {                                            \
		.a = __arr,                                          \
	};                                                                                \
	__iter.len = __iter.a->len;                                                       \
	for (__iter.e = __iter.a->len ? (struct obj_array_elem *)bucket_arr_get(&(__wk)->vm.objects.array_elems, __iter.a->head) : 0,\
			__val = __iter.e ? __iter.e->val : 0; __iter.i < __iter.len;            \
		__iter.e = __iter.e->next ? (struct obj_array_elem *)bucket_arr_get(&(__wk)->vm.objects.array_elems, __iter.e->next) : 0, \
	    __val = __iter.e ? __iter.e->val : 0,                                         \
	    ++__iter.i)

#define obj_array_for_array(__wk, __arr, __val) \
	obj_array_for_array_((__wk), __arr, __val, CONCAT(__iter, __LINE__))

#define obj_array_for_(__wk, __arr, __val, __iter) obj_array_for_array_(__wk, get_obj_array(__wk, __arr), __val, __iter)
#define obj_array_for(__wk, __arr, __val) obj_array_for_(__wk, __arr, __val, CONCAT(__iter, __LINE__))

/******************************************************************************
 * obj_dict_for
 ******************************************************************************/

struct obj_dict_for_helper {
	struct obj_dict *d;
	struct hash *h;
	struct obj_dict_elem *e;
	void *k;
	union obj_dict_big_dict_value v;
	uint32_t i;
	bool big;
};

#define obj_dict_for_get_kv_big(__iter, __key, __val)                                                \
	__iter.k = arr_get(&__iter.h->keys, __iter.i), __iter.v.u64 = *hash_get(__iter.h, __iter.k), \
	__key = __iter.v.val.key, __val = __iter.v.val.val

#define obj_dict_for_get_kv(__iter, __key, __val) __key = __iter.e->key, __val = __iter.e->val

#define obj_dict_for_dict_(__wk, __dict, __key, __val, __iter)                                                        \
	struct obj_dict_for_helper __iter = {                                                                         \
		.d = __dict,                                                                                          \
	};                                                                                                            \
	for (__key = 0,                                                                                               \
	    __val = 0,                                                                                                \
	    __iter.big = __iter.d->flags & obj_dict_flag_big,                                                         \
	    __iter.h = __iter.big ? (struct hash *)bucket_arr_get(&__wk->vm.objects.dict_hashes, __iter.d->data) : 0, \
	    __iter.e = __iter.big    ? 0 :                                                                            \
		       __iter.d->len ? (struct obj_dict_elem *)bucket_arr_get(                                        \
			       &__wk->vm.objects.dict_elems, __iter.d->data) :                                        \
				       0,                                                                             \
	    __iter.big ? (__iter.i < __iter.h->keys.len ? (obj_dict_for_get_kv_big(__iter, __key, __val)) : 0) :      \
			 (__iter.e ? (obj_dict_for_get_kv(__iter, __key, __val)) : 0);                                \
		__iter.big ? __iter.i < __iter.h->keys.len : !!__iter.e;                                              \
		__iter.big ? (++__iter.i,                                                                             \
			(__iter.i < __iter.h->keys.len ? (obj_dict_for_get_kv_big(__iter, __key, __val)) : 0)) :      \
			     (__iter.e = __iter.e->next ? (struct obj_dict_elem *)bucket_arr_get(                     \
						 &__wk->vm.objects.dict_elems, __iter.e->next) :                      \
							  0,                                                          \
			     (__iter.e ? (obj_dict_for_get_kv(__iter, __key, __val)) : 0)))

#define obj_dict_for_dict(__wk, __dict, __key, __val) \
	obj_dict_for_dict_((__wk), __dict, __key, __val, CONCAT(__iter, __LINE__))

#define obj_dict_for_(__wk, __dict, __key, __val, __iter) \
	obj_dict_for_dict_((__wk), get_obj_dict(__wk, __dict), __key, __val, __iter)
#define obj_dict_for(__wk, __dict, __key, __val) obj_dict_for_((__wk), __dict, __key, __val, CONCAT(__iter, __LINE__))

/******************************************************************************
 * obj_array_flat_for
 ******************************************************************************/

struct obj_array_flat_iter_ctx {
	struct obj_array_elem *e;
	uint32_t pushed;
	bool init;
};

#define obj_array_flat_for_(__wk, __arr, __val, __iter)                     \
	struct obj_array_flat_iter_ctx __iter = { 0 };                      \
	for (__val = obj_array_flat_iter_next(__wk, __arr, &__iter); __val; \
		__val = obj_array_flat_iter_next(__wk, __arr, &__iter))

#define obj_array_flat_for(__wk, __arr, __val) obj_array_flat_for_(__wk, __arr, __val, CONCAT(__iter, __LINE__))

struct workspace;
obj obj_array_flat_iter_next(struct workspace *wk, obj arr, struct obj_array_flat_iter_ctx *ctx);
void obj_array_flat_iter_end(struct workspace *wk, struct obj_array_flat_iter_ctx *ctx);

#endif

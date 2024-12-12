/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_ITERATORS_H
#define MUON_ITERATORS_H

enum iteration_result {
	ir_err,
	ir_cont,
	ir_done,
};

typedef enum iteration_result (*iterator_func)(void *ctx, void *val);

#define for_iter_(__ctx, __iter_type, __val, __iter)                                                    \
	struct iter_##__iter_type __iter = { 0 };                                                              \
	for (__val = iter_next_##__iter_type(__ctx, &__iter); iter_has_next_##__iter_type(__ctx, &__iter); \
		__val = iter_next_##__iter_type(__ctx, &__iter))

#define for_iter(__iter_type, __ctx, __val) for_iter_(__ctx, __iter_type, __val, CONCAT(__iter, __LINE__))

#endif

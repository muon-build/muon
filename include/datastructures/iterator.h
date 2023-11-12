/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_ITERATOR_H
#define MUON_ITERATOR_H
enum iteration_result {
	ir_err,
	ir_cont,
	ir_done,
};

typedef enum iteration_result (*iterator_func)(void *ctx, void *val);
#endif


/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_COMPILER_H
#define MUON_LANG_COMPILER_H

#include "lang/types.h"

struct source;
struct workspace;

struct local_binding {
	struct node *n;
	obj id;
	uint32_t depth, slot;
	bool bound, accessed;
};

struct upvalue_binding {
	uint32_t depth, slot;
	obj id;
	bool is_local;
};

struct compiler_call_frame {
	uint32_t locals_base, upvalues_base;
	uint32_t nupvalues;
	struct func_upvalue *upvalues;
	obj *locals_debug;
};

enum vm_compile_mode {
	vm_compile_mode_fmt = 1 << 1,
	vm_compile_mode_quiet = 1 << 2,
	vm_compile_mode_language_extended = 1 << 3,
	vm_compile_mode_expr = 1 << 4,
	vm_compile_mode_return_after_project = 1 << 5,
	vm_compile_mode_relaxed_parse = 1 << 6,
	vm_compile_mode_locals = 1 << 7,
};

void vm_compile_state_reset(struct workspace *wk);
struct node;
bool
vm_compile_ast(struct workspace *wk, struct node *n, enum vm_compile_mode mode, const struct args_norm *an, uint32_t *entry);
bool vm_compile(struct workspace *wk, const struct source *src, enum vm_compile_mode mode, uint32_t *entry);
#endif

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_FUNC_LOOKUP_H
#define MUON_LANG_FUNC_LOOKUP_H

#include "lang/workspace.h"

typedef bool (*func_impl)(struct workspace *wk, obj self, obj *res);
typedef obj (*func_impl_self_transform)(struct workspace *wk, obj self);

struct func_impl {
	const char *name;
	func_impl func;
	type_tag return_type;
	bool pure, fuzz_unsafe, extension;
	func_impl_self_transform self_transform;
};

struct func_impl_group {
	const struct func_impl *impls;
	uint32_t off, len;
};

extern struct func_impl_group func_impl_groups[obj_type_count][language_mode_count];
extern struct func_impl native_funcs[];

void build_func_impl_tables(void);

bool func_lookup(struct workspace *wk, obj self, const char *name, uint32_t *idx, obj *func);
bool func_lookup_for_group(const struct func_impl_group impl_group[],
	enum language_mode mode,
	const char *name,
	uint32_t *idx);

void dump_function_signature(struct workspace *wk, struct args_norm posargs[], struct args_kw kwargs[]);
void dump_function_signatures(struct workspace *wk);
#endif

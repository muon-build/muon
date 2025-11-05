/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_FUNC_LOOKUP_H
#define MUON_LANG_FUNC_LOOKUP_H

#include "lang/workspace.h"
#include "platform/assert.h"

typedef bool (*func_native_impl)(struct workspace *wk, obj self, obj *res);
typedef obj (*func_impl_self_transform)(struct workspace *wk, obj self);

enum func_impl_flag {
	func_impl_flag_sandbox_disable = 1 << 0,
	func_impl_flag_extension = 1 << 1,
	func_impl_flag_throws_error = 1 << 2,
	func_impl_flag_impure = 1 << 3,
};

struct func_impl {
	const char *name;
	func_native_impl func;
	type_tag return_type;
	enum func_impl_flag flags;
	func_impl_self_transform self_transform;
	const char *desc;
	const char *file;
	uint32_t line;
	const char *deferred_return_type;
};

#define FUNC_IMPL(__type, __name, ...) \
static bool func_ ## __type ## _ ## __name(struct workspace *wk, obj self, obj *res); \
static struct func_impl func_impl_ ## __type ## _ ## __name = { #__name, func_ ## __type ## _ ## __name, __VA_ARGS__, .file = __FILE__, .line = __LINE__ }; \
static bool func_ ## __type ## _ ## __name(struct workspace *wk, obj self, obj *res)

#define FUNC_IMPL_RETURN(k) 0, .deferred_return_type = #k

#define FUNC_IMPL_REGISTER_ARGS struct workspace *wk, enum language_mode lang_mode, struct func_impl *dest, uint32_t cap, uint32_t *added
#define FUNC_IMPL_REGISTER_ARGS_FWD wk, lang_mode, dest, cap, added

void func_impl_register(FUNC_IMPL_REGISTER_ARGS, const struct func_impl *src, const char *alias);

// These functions also define integer variables so that we get errors on
// duplicate registration

#define FUNC_IMPL_REGISTER(__type, __name) \
int dup_func_impl_ ## __type ## _ ## __name; (void)dup_func_impl_ ## __type ## _ ## __name; \
func_impl_register(FUNC_IMPL_REGISTER_ARGS_FWD, &func_impl_ ## __type ## _ ## __name, 0)

#define FUNC_IMPL_REGISTER_ALIAS(__type, __name, __alias) \
int dup_func_impl_ ## __type ## __alias; (void)dup_func_impl_ ## __type ## __alias; \
func_impl_register(FUNC_IMPL_REGISTER_ARGS_FWD, &func_impl_ ## __type ## _ ## __name, #__alias)

#define FUNC_REGISTER(__type) void func_impl_register_ ## __type(FUNC_IMPL_REGISTER_ARGS)
typedef void ((*func_impl_register_proto)(FUNC_IMPL_REGISTER_ARGS));
void func_impl_register_inherit(func_impl_register_proto reg, func_impl_self_transform self_transform, FUNC_IMPL_REGISTER_ARGS);
#define FUNC_REGISTER_INHERIT(__type, __self_transform) func_impl_register_inherit(func_impl_register_ ## __type, __self_transform, FUNC_IMPL_REGISTER_ARGS_FWD)

struct func_impl_group {
	const struct func_impl *impls;
	uint32_t off, len;
};

extern struct func_impl native_funcs[];

void build_func_impl_tables(struct workspace *wk);

bool func_lookup(struct workspace *wk, obj self, const char *name, uint32_t *idx, obj *func);
bool func_lookup_for_group(const struct func_impl_group impl_group[],
	enum language_mode mode,
	const char *name,
	uint32_t *idx);
const struct func_impl_group *func_lookup_group(enum obj_type t);

void func_kwargs_lookup(struct workspace *wk, obj self, const char *name, struct arr *kwargs_arr);
void kwargs_arr_init(struct workspace *wk, struct arr *arr);
void kwargs_arr_push(struct workspace *wk, struct arr *arr, const struct args_kw *kw);
void kwargs_arr_del(struct workspace *wk, struct arr *arr, const char *name);
struct args_kw *kwargs_arr_get(struct workspace *wk, struct arr *arr, const char *name);

void dump_function_signatures(struct workspace *wk);
void dump_function_docs(struct workspace *wk);

obj dump_function_native(struct workspace *wk, enum obj_type t, const struct func_impl *impl);
obj dump_module_function_native(struct workspace *wk, enum module module, const struct func_impl *impl);
obj dump_module_function_capture(struct workspace *wk, const char *module, obj name, obj o);
#endif

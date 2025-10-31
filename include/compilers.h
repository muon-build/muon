/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-FileCopyrightText: Owen Rafferty <owen@owenrafferty.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_COMPILERS_H
#define MUON_COMPILERS_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/types.h"
#include "machines.h"

struct workspace;
struct obj_compiler;

#define FOREACH_COMPILER_EXPOSED_LANGUAGE(_) \
	_(c)                                 \
	_(cpp)                               \
	_(objc)                              \
	_(objcpp)                            \
	_(assembly)                          \
	_(llvm_ir)                           \
	_(nasm)

#define TOOLCHAIN_ENUM(lang) compiler_language_##lang,
enum compiler_language {
	compiler_language_null,
	FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
	compiler_language_c_hdr,
	compiler_language_cpp_hdr,
	compiler_language_objc_hdr,
	compiler_language_objcpp_hdr,
	compiler_language_c_obj,
	compiler_language_count,
};
#undef TOOLCHAIN_ENUM

enum compiler_optimization_lvl {
	compiler_optimization_lvl_none,
	compiler_optimization_lvl_0,
	compiler_optimization_lvl_1,
	compiler_optimization_lvl_2,
	compiler_optimization_lvl_3,
	compiler_optimization_lvl_g,
	compiler_optimization_lvl_s,
};

enum compiler_pgo_stage {
	compiler_pgo_generate,
	compiler_pgo_use,
};

enum compiler_warning_lvl {
	compiler_warning_lvl_0,
	compiler_warning_lvl_1,
	compiler_warning_lvl_2,
	compiler_warning_lvl_3,
	compiler_warning_lvl_everything,
};

enum compiler_visibility_type {
	compiler_visibility_default,
	compiler_visibility_hidden,
	compiler_visibility_internal,
	compiler_visibility_protected,
	compiler_visibility_inlineshidden,
};

enum toolchain_arg_arity {
	toolchain_arg_arity_0,
	toolchain_arg_arity_1i,
	toolchain_arg_arity_1s,
	toolchain_arg_arity_2s,
	toolchain_arg_arity_1s1b,
	toolchain_arg_arity_ns,
	toolchain_arg_arity_0rb,
	toolchain_arg_arity_1srb,
};

struct toolchain_arg_handler {
	const char *name;
	enum toolchain_arg_arity arity;
};

#define TOOLCHAIN_TRUE ((void *)1)
#define TOOLCHAIN_FALSE 0

#define TOOLCHAIN_PARAMS_BASE struct workspace *wk, struct obj_compiler *comp
#define TOOLCHAIN_PARAM_NAMES_BASE wk, comp

#define TOOLCHAIN_SIG_0 TOOLCHAIN_PARAMS_BASE
#define TOOLCHAIN_SIG_1i TOOLCHAIN_PARAMS_BASE, uint32_t i1
#define TOOLCHAIN_SIG_1s TOOLCHAIN_PARAMS_BASE, const char *s1
#define TOOLCHAIN_SIG_2s TOOLCHAIN_PARAMS_BASE, const char *s1, const char *s2
#define TOOLCHAIN_SIG_1s1b TOOLCHAIN_PARAMS_BASE, const char *s1, bool b1
#define TOOLCHAIN_SIG_ns TOOLCHAIN_PARAMS_BASE, const struct args *n1
#define TOOLCHAIN_SIG_0rb TOOLCHAIN_PARAMS_BASE
#define TOOLCHAIN_SIG_1srb TOOLCHAIN_PARAMS_BASE, const char *s1

#define TOOLCHAIN_ARGS_RETURN const struct args *
#define TOOLCHAIN_PARAMS_0 TOOLCHAIN_ARGS_RETURN, 0, (TOOLCHAIN_SIG_0), (TOOLCHAIN_PARAM_NAMES_BASE)
#define TOOLCHAIN_PARAMS_1i TOOLCHAIN_ARGS_RETURN, 1i, (TOOLCHAIN_SIG_1i), (TOOLCHAIN_PARAM_NAMES_BASE, i1)
#define TOOLCHAIN_PARAMS_1s TOOLCHAIN_ARGS_RETURN, 1s, (TOOLCHAIN_SIG_1s), (TOOLCHAIN_PARAM_NAMES_BASE, s1)
#define TOOLCHAIN_PARAMS_2s TOOLCHAIN_ARGS_RETURN, 2s, (TOOLCHAIN_SIG_2s), (TOOLCHAIN_PARAM_NAMES_BASE, s1, s2)
#define TOOLCHAIN_PARAMS_1s1b TOOLCHAIN_ARGS_RETURN, 1s1b, (TOOLCHAIN_SIG_1s1b), (TOOLCHAIN_PARAM_NAMES_BASE, s1, b1)
#define TOOLCHAIN_PARAMS_ns TOOLCHAIN_ARGS_RETURN, ns, (TOOLCHAIN_SIG_ns), (TOOLCHAIN_PARAM_NAMES_BASE, n1)
#define TOOLCHAIN_PARAMS_0rb bool, 0rb, (TOOLCHAIN_SIG_0rb), (TOOLCHAIN_PARAM_NAMES_BASE)
#define TOOLCHAIN_PARAMS_1srb bool, 1srb, (TOOLCHAIN_SIG_1srb), (TOOLCHAIN_PARAM_NAMES_BASE, s1)

typedef const struct args *((*compiler_get_arg_func_0)(TOOLCHAIN_SIG_0));
typedef const struct args *((*compiler_get_arg_func_1i)(TOOLCHAIN_SIG_1i));
typedef const struct args *((*compiler_get_arg_func_1s)(TOOLCHAIN_SIG_1s));
typedef const struct args *((*compiler_get_arg_func_2s)(TOOLCHAIN_SIG_2s));
typedef const struct args *((*compiler_get_arg_func_1s1b)(TOOLCHAIN_SIG_1s1b));
typedef const struct args *((*compiler_get_arg_func_ns)(TOOLCHAIN_SIG_ns));
typedef bool ((*compiler_get_arg_func_0rb)(TOOLCHAIN_SIG_0rb));
typedef bool ((*compiler_get_arg_func_1srb)(TOOLCHAIN_SIG_1srb));

#define TOOLCHAIN_ARG_MEMBER_(name, return_type, type, params, names) compiler_get_arg_func_##type name;
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, type)

#define FOREACH_COMPILER_ARG(_)                                \
	_(do_linker_passthrough, compiler, TOOLCHAIN_PARAMS_0rb) \
	_(check_ignored_option, compiler, TOOLCHAIN_PARAMS_1srb) \
	_(linker_passthrough, compiler, TOOLCHAIN_PARAMS_ns)   \
	_(linker_delimiter, compiler, TOOLCHAIN_PARAMS_0)   \
	_(deps, compiler, TOOLCHAIN_PARAMS_2s)                 \
	_(compile_only, compiler, TOOLCHAIN_PARAMS_0)          \
	_(preprocess_only, compiler, TOOLCHAIN_PARAMS_0)       \
	_(output, compiler, TOOLCHAIN_PARAMS_1s)               \
	_(optimization, compiler, TOOLCHAIN_PARAMS_1i)         \
	_(debug, compiler, TOOLCHAIN_PARAMS_0)                 \
	_(warning_lvl, compiler, TOOLCHAIN_PARAMS_1i)          \
	_(warn_everything, compiler, TOOLCHAIN_PARAMS_0)       \
	_(werror, compiler, TOOLCHAIN_PARAMS_0)                \
	_(set_std, compiler, TOOLCHAIN_PARAMS_1s)              \
	_(include, compiler, TOOLCHAIN_PARAMS_1s)              \
	_(include_system, compiler, TOOLCHAIN_PARAMS_1s)       \
	_(include_dirafter, compiler, TOOLCHAIN_PARAMS_1s)     \
	_(pgo, compiler, TOOLCHAIN_PARAMS_1i)                  \
	_(pic, compiler, TOOLCHAIN_PARAMS_0)                   \
	_(pie, compiler, TOOLCHAIN_PARAMS_0)                   \
	_(sanitize, compiler, TOOLCHAIN_PARAMS_1s)             \
	_(define, compiler, TOOLCHAIN_PARAMS_1s)               \
	_(visibility, compiler, TOOLCHAIN_PARAMS_1i)           \
	_(specify_lang, compiler, TOOLCHAIN_PARAMS_1s)         \
	_(color_output, compiler, TOOLCHAIN_PARAMS_1s)         \
	_(enable_lto, compiler, TOOLCHAIN_PARAMS_0)            \
	_(always, compiler, TOOLCHAIN_PARAMS_0)                \
	_(crt, compiler, TOOLCHAIN_PARAMS_1s1b)                \
	_(debugfile, compiler, TOOLCHAIN_PARAMS_1s)            \
	_(object_ext, compiler, TOOLCHAIN_PARAMS_0)            \
	_(pch_ext, compiler, TOOLCHAIN_PARAMS_0)               \
	_(force_language, compiler, TOOLCHAIN_PARAMS_1i)       \
	_(deps_type, compiler, TOOLCHAIN_PARAMS_0)             \
	_(coverage, compiler, TOOLCHAIN_PARAMS_0)              \
	_(std_supported, compiler, TOOLCHAIN_PARAMS_1s)        \
	_(permissive, compiler, TOOLCHAIN_PARAMS_0)            \
	_(include_pch, compiler, TOOLCHAIN_PARAMS_1s)          \
	_(emit_pch, compiler, TOOLCHAIN_PARAMS_0)              \
	_(winvalid_pch, compiler, TOOLCHAIN_PARAMS_0)          \
	_(can_compile_llvm_ir, compiler, TOOLCHAIN_PARAMS_0rb) \
	_(argument_syntax, compiler, TOOLCHAIN_PARAMS_0)

#define FOREACH_LINKER_ARG(_)                                \
	_(check_ignored_option, linker, TOOLCHAIN_PARAMS_1srb) \
	_(lib, linker, TOOLCHAIN_PARAMS_1s)                  \
	_(debug, linker, TOOLCHAIN_PARAMS_0)                 \
	_(as_needed, linker, TOOLCHAIN_PARAMS_0)             \
	_(no_undefined, linker, TOOLCHAIN_PARAMS_0)          \
	_(start_group, linker, TOOLCHAIN_PARAMS_0)           \
	_(end_group, linker, TOOLCHAIN_PARAMS_0)             \
	_(shared, linker, TOOLCHAIN_PARAMS_0)                \
	_(soname, linker, TOOLCHAIN_PARAMS_1s)               \
	_(rpath, linker, TOOLCHAIN_PARAMS_1s)                \
	_(pgo, linker, TOOLCHAIN_PARAMS_1i)                  \
	_(sanitize, linker, TOOLCHAIN_PARAMS_1s)             \
	_(allow_shlib_undefined, linker, TOOLCHAIN_PARAMS_0) \
	_(shared_module, linker, TOOLCHAIN_PARAMS_0)         \
	_(export_dynamic, linker, TOOLCHAIN_PARAMS_0)        \
	_(fatal_warnings, linker, TOOLCHAIN_PARAMS_0)        \
	_(whole_archive, linker, TOOLCHAIN_PARAMS_1s)        \
	_(enable_lto, linker, TOOLCHAIN_PARAMS_0)            \
	_(input_output, linker, TOOLCHAIN_PARAMS_2s)         \
	_(always, linker, TOOLCHAIN_PARAMS_0)                \
	_(coverage, linker, TOOLCHAIN_PARAMS_0)              \
	_(implib, linker, TOOLCHAIN_PARAMS_1s)               \
	_(def, linker, TOOLCHAIN_PARAMS_1s) \
	_(fuse_ld, linker, TOOLCHAIN_PARAMS_0)

#define FOREACH_STATIC_LINKER_ARG(_)                        \
	_(needs_wipe, static_linker, TOOLCHAIN_PARAMS_0rb)          \
	_(base, static_linker, TOOLCHAIN_PARAMS_0)          \
	_(input_output, static_linker, TOOLCHAIN_PARAMS_2s) \
	_(always, static_linker, TOOLCHAIN_PARAMS_0)

struct language {
	bool is_header;
	bool is_linkable;
};

struct compiler {
	struct {
		FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER)
	} args;
	uint32_t default_linker;
	uint32_t default_static_linker;
};

struct linker {
	struct {
		FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
	} args;
};

struct static_linker {
	struct {
		FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
	} args;
};

#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

enum toolchain_arg {
#define TOOLCHAIN_ARG_MEMBER_(comp, _name) toolchain_arg_##comp##_name,
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(comp, _##name)
	FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER) FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
		FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_
};

struct toolchain_id {
	const char *id;
	const char *public_id;
};

struct toolchain_registry_component {
	struct toolchain_id id;
	obj detect, overrides;
};

extern const struct language languages[];

struct compiler_check_cache_key {
	struct obj_compiler *comp;
	const char *argstr;
	const char *src;
	uint32_t argc;
};

struct compiler_check_cache_value {
	obj value;
	bool success;
};

obj compiler_check_cache_key(struct workspace *wk, const struct compiler_check_cache_key *key);
bool compiler_check_cache_get(struct workspace *wk, obj key, struct compiler_check_cache_value *val);
void compiler_check_cache_set(struct workspace *wk, obj key, const struct compiler_check_cache_value *val);

bool toolchain_type_from_s(struct workspace *wk, enum toolchain_component comp, const char *name, uint32_t *res);
const struct toolchain_id *toolchain_component_type_to_id(struct workspace *wk, enum toolchain_component comp, uint32_t val);
const char *toolchain_component_to_s(enum toolchain_component comp);
bool toolchain_component_from_s(const char *name, uint32_t *res);
const char *compiler_type_to_s(struct workspace *wk, uint32_t compiler_type);
bool compiler_type_from_s(struct workspace *wk, const char *name, uint32_t *res);
uint32_t compiler_type(struct workspace *wk, const char *name);
const char *linker_type_to_s(struct workspace *wk, uint32_t linker_type);
bool linker_type_from_s(struct workspace *wk, const char *name, uint32_t *res);
bool static_linker_type_from_s(struct workspace *wk, const char *name, uint32_t *res);

enum compiler_language compiler_language_to_hdr(enum compiler_language lang);
const char *compiler_language_to_s(enum compiler_language l);
bool s_to_compiler_language(const char *s, enum compiler_language *l);

bool filename_to_compiler_language(const char *str, enum compiler_language *l);
const char *compiler_language_extension(enum compiler_language l);
enum compiler_language coalesce_link_languages(enum compiler_language cur, enum compiler_language new_lang);

bool
toolchain_register_component(struct workspace *wk,
	enum toolchain_component component,
	const struct toolchain_registry_component *base,
	const void *data);

bool toolchain_detect(struct workspace *wk, obj *comp, enum machine_kind machine, enum compiler_language lang);
void compilers_init(struct workspace *wk);

const struct toolchain_arg_handler *get_toolchain_arg_handler_info(enum toolchain_component component,
	const char *name);
bool toolchain_overrides_validate(struct workspace *wk, uint32_t ip, obj handlers, enum toolchain_component component);

struct toolchain_dump_opts {
	const char *s1, *s2;
	bool b1;
	uint32_t i1;
	const struct args *n1;
};
void toolchain_dump(struct workspace *wk, struct obj_compiler *comp, struct toolchain_dump_opts *opts);

#define TOOLCHAIN_ARG_MEMBER_(name, _name, component, return_type, _type, params, names) \
	return_type toolchain_##component##_name params;
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, _##name, comp, type)

FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER)
FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)

#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

const char *compiler_log_prefix(enum compiler_language lang, enum machine_kind machine);

#endif

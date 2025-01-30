/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_TYPES_H
#define MUON_LANG_TYPES_H
#include <stdbool.h>
#include <stdint.h>

typedef uint32_t obj;
typedef uint64_t type_tag;

struct args_norm {
	type_tag type;
	const char *name;
	const char *desc;
	obj val;
	uint32_t node;
	bool set, optional;
};

struct args_kw {
	const char *key;
	type_tag type;
	const char *desc;
	obj val;
	uint32_t node;
	bool set;
	bool required;
	bool extension;
};

enum language_mode {
	language_external,
	language_internal,
	language_opts,
	language_mode_count,

	language_extended,
};

enum build_language {
	build_language_meson,
	build_language_cmake,
};

enum log_level {
	log_quiet,
	log_error,
	log_warn,
	log_info,
	log_debug,
	log_level_count,
};

enum toolchain_component {
	toolchain_component_compiler,
	toolchain_component_linker,
	toolchain_component_static_linker,
};
#define toolchain_component_count 3 // Keep in sync with above

struct complex_types {
	type_tag options_dict_or_list;
};

union obj_dict_big_dict_value {
	uint64_t u64;
	struct {
		obj key, val;
	} val;
};

#endif

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-FileCopyrightText: Owen Rafferty <owen@owenrafferty.com>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "compilers.h"
#include "error.h"
#include "guess.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "machines.h"
#include "options.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

struct toolchain_id {
	const char *public_id;
	const char *id;
};
#define TOOLCHAIN_NAME(_, public_id, id) { public_id, id },
static const struct toolchain_id compiler_type_name[] = { FOREACH_TOOLCHAIN_COMPILER_TYPE(TOOLCHAIN_NAME) };
static const struct toolchain_id linker_type_name[] = { FOREACH_TOOLCHAIN_LINKER_TYPE(TOOLCHAIN_NAME) };
static const struct toolchain_id static_linker_type_name[] = { FOREACH_TOOLCHAIN_STATIC_LINKER_TYPE(TOOLCHAIN_NAME) };
#undef TOOLCHAIN_CASE

static bool
toolchain_id_lookup(const char *name, const struct toolchain_id names[], uint32_t len, uint32_t *res)
{
	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (strcmp(name, names[i].id) == 0) {
			*res = i;
			return true;
		}
	}

	return false;
}

const char *
compiler_type_to_s(enum compiler_type t)
{
	return compiler_type_name[t].public_id;
}

bool
compiler_type_from_s(const char *name, uint32_t *res)
{
	return toolchain_id_lookup(name, compiler_type_name, ARRAY_LEN(compiler_type_name), res);
}

const char *
linker_type_to_s(enum linker_type t)
{
	return linker_type_name[t].public_id;
}

bool
linker_type_from_s(const char *name, uint32_t *res)
{
	return toolchain_id_lookup(name, linker_type_name, ARRAY_LEN(linker_type_name), res);
}

static const char *
static_linker_type_to_s(enum static_linker_type t)
{
	return static_linker_type_name[t].public_id;
}

bool
static_linker_type_from_s(const char *name, uint32_t *res)
{
	return toolchain_id_lookup(name, static_linker_type_name, ARRAY_LEN(static_linker_type_name), res);
}

static const char *compiler_language_names[compiler_language_count] = {
	[compiler_language_null] = "null",
	[compiler_language_c] = "c",
	[compiler_language_c_hdr] = "c_hdr",
	[compiler_language_cpp] = "cpp",
	[compiler_language_cpp_hdr] = "cpp_hdr",
	[compiler_language_c_obj] = "c_obj",
	[compiler_language_objc] = "objc",
	[compiler_language_objcpp] = "objcpp",
	[compiler_language_assembly] = "assembly",
	[compiler_language_llvm_ir] = "llvm_ir",
	[compiler_language_nasm] = "nasm",
};

const char *
compiler_language_to_s(enum compiler_language l)
{
	assert(l < compiler_language_count);
	return compiler_language_names[l];
}

bool
s_to_compiler_language(const char *_s, enum compiler_language *l)
{
	const struct str s = WKSTR(_s);
	uint32_t i;
	for (i = 0; i < compiler_language_count; ++i) {
		if (str_eqli(&s, &WKSTR(compiler_language_names[i]))) {
			*l = i;
			return true;
		}
	}

	return false;
}

static const char *compiler_language_exts[compiler_language_count][10] = {
	[compiler_language_c] = { "c" },
	[compiler_language_c_hdr] = { "h" },
	[compiler_language_cpp] = { "cc", "cpp", "cxx", "C" },
	[compiler_language_cpp_hdr] = { "hh", "hpp", "hxx" },
	[compiler_language_c_obj] = { "o", "obj" },
	[compiler_language_objc] = { "m", "M" },
	[compiler_language_objcpp] = { "mm" },
	[compiler_language_assembly] = { "S", "s" },
	[compiler_language_llvm_ir] = { "ll" },
	[compiler_language_nasm] = { "asm" },
};

bool
filename_to_compiler_language(const char *str, enum compiler_language *l)
{
	uint32_t i, j;
	const char *ext;

	if (!(ext = strrchr(str, '.'))) {
		return false;
	}
	++ext;

	for (i = 0; i < compiler_language_count; ++i) {
		for (j = 0; compiler_language_exts[i][j]; ++j) {
			if (strcmp(ext, compiler_language_exts[i][j]) == 0) {
				*l = i;
				return true;
			}
		}
	}

	return false;
}

const char *
compiler_language_extension(enum compiler_language l)
{
	return compiler_language_exts[l][0];
}

enum compiler_language
coalesce_link_languages(enum compiler_language cur, enum compiler_language new)
{
	switch (new) {
	case compiler_language_null:
	case compiler_language_c_hdr:
	case compiler_language_cpp_hdr:
	case compiler_language_llvm_ir: break;
	case compiler_language_assembly:
		if (!cur) {
			return compiler_language_assembly;
		}
		break;
	case compiler_language_nasm:
	case compiler_language_c:
	case compiler_language_c_obj:
	case compiler_language_objc:
		if (!cur) {
			return compiler_language_c;
		}
		break;
	case compiler_language_cpp:
	case compiler_language_objcpp:
		if (!cur || cur == compiler_language_c || cur == compiler_language_assembly) {
			return compiler_language_cpp;
		}
		break;
	case compiler_language_count: UNREACHABLE;
	}

	return cur;
}

static bool
run_cmd_arr(struct workspace *wk, struct run_cmd_ctx *cmd_ctx, obj cmd_arr, const char *arg)
{
	obj args;
	obj_array_dup(wk, cmd_arr, &args);
	obj_array_push(wk, args, make_str(wk, arg));

	const char *argstr;
	uint32_t argc;
	join_args_argstr(wk, &argstr, &argc, args);

	if (!run_cmd(cmd_ctx, argstr, argc, NULL, 0)) {
		run_cmd_ctx_destroy(cmd_ctx);
		return false;
	}

	return true;
}

static const char *
guess_version_arg(struct workspace *wk, bool msvc_like)
{
	if (msvc_like) {
		return "/?";
	} else {
		return "--version";
	}
}

static bool
compiler_detect_c_or_cpp(struct workspace *wk, obj cmd_arr, obj comp_id)
{
	bool msvc_like = obj_array_in(wk, cmd_arr, make_str(wk, "cl"));

	// helpful: mesonbuild/compilers/detect.py:350
	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd_arr(wk, &cmd_ctx, cmd_arr, guess_version_arg(wk, msvc_like))) {
		run_cmd_ctx_destroy(&cmd_ctx);
		return false;
	}

	enum compiler_type type;
	bool unknown = true;
	obj ver;

	if (cmd_ctx.status != 0) {
		goto detection_over;
	}

	if (strstr(cmd_ctx.out.buf, "clang") || strstr(cmd_ctx.out.buf, "Clang")) {
		if (strstr(cmd_ctx.out.buf, "Apple") && strstr(cmd_ctx.out.buf, "clang")) {
			type = compiler_apple_clang;
		} else if (strstr(cmd_ctx.out.buf, "CL.EXE COMPATIBILITY")) {
			type = compiler_clang_cl;
		} else {
			type = compiler_clang;
		}
	} else if (strstr(cmd_ctx.out.buf, "Free Software Foundation")) {
		type = compiler_gcc;
	} else if (strstr(cmd_ctx.out.buf, "Microsoft") || strstr(cmd_ctx.err.buf, "Microsoft")) {
		type = compiler_msvc;
	} else {
		goto detection_over;
	}

	if (!guess_version(wk, (type == compiler_msvc) ? cmd_ctx.err.buf : cmd_ctx.out.buf, &ver)) {
		ver = make_str(wk, "unknown");
	}

	unknown = false;

detection_over:
	if (unknown) {
		LOG_W("unable to detect compiler type, falling back on posix compiler");
		type = compiler_posix;
		ver = make_str(wk, "unknown");
	}

	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	comp->cmd_arr = cmd_arr;
	comp->type[toolchain_component_compiler] = type;
	comp->ver = ver;

	run_cmd_ctx_destroy(&cmd_ctx);
	return true;
}

static bool
compiler_detect_nasm(struct workspace *wk, obj cmd_arr, obj comp_id)
{
	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd_arr(wk, &cmd_ctx, cmd_arr, "--version")
		|| strstr(cmd_ctx.err.buf, "nasm: error: unable to find utility")) {
		run_cmd_ctx_destroy(&cmd_ctx);
		return false;
	}

	enum compiler_type type;
	obj ver;

	if (strstr(cmd_ctx.out.buf, "NASM")) {
		type = compiler_nasm;
	} else if (strstr(cmd_ctx.out.buf, "yasm")) {
		type = compiler_yasm;
	} else {
		// Just assume it is nasm
		type = compiler_nasm;
	}

	if (!guess_version(wk, cmd_ctx.out.buf, &ver)) {
		ver = make_str(wk, "unknown");
	}

	obj new_cmd;
	obj_array_dup(wk, cmd_arr, &new_cmd);

	{
		uint32_t addr_bits = host_machine.address_bits;

		const char *plat;
		SBUF(define);

		if (host_machine.is_windows) {
			plat = "win";
			sbuf_pushf(wk, &define, "WIN%d", addr_bits);
		} else if (host_machine.sys == machine_system_darwin) {
			plat = "macho";
			sbuf_pushs(wk, &define, "MACHO");
		} else {
			plat = "elf";
			sbuf_pushs(wk, &define, "ELF");
		}

		obj_array_push(wk, new_cmd, make_strf(wk, "-f%s%d", plat, addr_bits));
		obj_array_push(wk, new_cmd, make_strf(wk, "-D%s", define.buf));
		if (addr_bits == 64) {
			obj_array_push(wk, new_cmd, make_str(wk, "-D__x86_64__"));
		}
	}

	struct obj_compiler *comp = get_obj_compiler(wk, comp_id);
	comp->cmd_arr = new_cmd;
	comp->type[toolchain_component_compiler] = type;
	comp->ver = ver;
	comp->lang = compiler_language_nasm;

	run_cmd_ctx_destroy(&cmd_ctx);
	return true;
}

static bool
compiler_get_libdirs(struct workspace *wk, struct obj_compiler *comp)
{
	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd_arr(wk, &cmd_ctx, comp->cmd_arr, "-print-search-dirs") || cmd_ctx.status) {
		goto done;
	}

	const char *key = "libraries: ";
	char *s, *e;
	bool beginning_of_line = true;
	for (s = cmd_ctx.out.buf; *s; ++s) {
		if (beginning_of_line && strncmp(s, key, strlen(key)) == 0) {
			s += strlen(key);
			if (*s == '=') {
				++s;
			}

			e = strchr(s, '\n');

			struct str str = {
				.s = s,
				.len = e ? (uint32_t)(e - s) : strlen(s),
			};

			comp->libdirs = str_split(wk, &str, &WKSTR(ENV_PATH_SEP_STR));
			goto done;
		}

		beginning_of_line = *s == '\n';
	}

done:
	run_cmd_ctx_destroy(&cmd_ctx);

	if (!comp->libdirs) {
		const char *libdirs[] = { "/usr/lib", "/usr/local/lib", "/lib", NULL };

		make_obj(wk, &comp->libdirs, obj_array);

		uint32_t i;
		for (i = 0; libdirs[i]; ++i) {
			obj_array_push(wk, comp->libdirs, make_str(wk, libdirs[i]));
		}
	}

	return true;
}

static void
compiler_refine_host_machine(struct workspace *wk, struct obj_compiler *comp)
{
	struct run_cmd_ctx cmd_ctx = { 0 };
	if (run_cmd_arr(wk, &cmd_ctx, comp->cmd_arr, "-dumpmachine") && cmd_ctx.status == 0) {
		machine_parse_and_apply_triplet(&host_machine, cmd_ctx.out.buf);
	}
	run_cmd_ctx_destroy(&cmd_ctx);

	// TODO: check for macros like ILP32 and x86_64
}

static bool
compiler_detect_cmd_arr(struct workspace *wk, obj comp, enum compiler_language lang, obj cmd_arr)
{
	if (log_should_print(log_debug)) {
		obj_fprintf(wk, log_file(), "checking compiler %o\n", cmd_arr);
	}

	switch (lang) {
	case compiler_language_c:
	case compiler_language_cpp:
	case compiler_language_objc:
	case compiler_language_objcpp:
		if (!compiler_detect_c_or_cpp(wk, cmd_arr, comp)) {
			return false;
		}

		struct obj_compiler *compiler = get_obj_compiler(wk, comp);
		compiler_get_libdirs(wk, compiler);

		compiler_refine_host_machine(wk, compiler);

		compiler->lang = lang;

		compiler->linker_passthrough = compiler->type[toolchain_component_compiler] != compiler_msvc;
		return true;
	case compiler_language_nasm:
		if (!compiler_detect_nasm(wk, cmd_arr, comp)) {
			return false;
		}
		return true;
	default:
		LOG_E("tried to get a compiler for unsupported language '%s'", compiler_language_to_s(lang));
		return false;
	}
}

static bool
static_linker_detect(struct workspace *wk, obj comp, enum compiler_language lang, obj cmd_arr)
{
	struct obj_compiler *compiler = get_obj_compiler(wk, comp);

	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd_arr(wk,
		    &cmd_ctx,
		    cmd_arr,
		    guess_version_arg(wk, compiler->type[toolchain_component_compiler] == compiler_msvc))) {
		run_cmd_ctx_destroy(&cmd_ctx);
		return false;
	}

	enum static_linker_type type = compiler->type[toolchain_component_compiler] == compiler_msvc ?
					       static_linker_msvc :
					       static_linker_ar_posix;

	if (cmd_ctx.status == 0 && strstr(cmd_ctx.out.buf, "Free Software Foundation")) {
		type = static_linker_ar_gcc;
	}

	run_cmd_ctx_destroy(&cmd_ctx);

	get_obj_compiler(wk, comp)->static_linker_cmd_arr = cmd_arr;
	get_obj_compiler(wk, comp)->type[toolchain_component_static_linker] = type;
	return true;
}

static bool
linker_detect(struct workspace *wk, obj comp, enum compiler_language lang, obj cmd_arr)
{
	struct obj_compiler *compiler = get_obj_compiler(wk, comp);

	bool msvc_like = compiler->type[toolchain_component_compiler] == compiler_msvc;

	enum linker_type type = compilers[compiler->type[toolchain_component_compiler]].default_linker;
	if (host_machine.sys == machine_system_windows) {
		if (compiler->type[toolchain_component_compiler] == compiler_clang) {
			type = linker_lld_link;
			msvc_like = true;
		}
	}

	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd_arr(wk, &cmd_ctx, cmd_arr, guess_version_arg(wk, msvc_like))) {
		run_cmd_ctx_destroy(&cmd_ctx);
		return false;
	}

	// TODO: do something with command output?

	run_cmd_ctx_destroy(&cmd_ctx);

	get_obj_compiler(wk, comp)->linker_cmd_arr = cmd_arr;
	get_obj_compiler(wk, comp)->type[toolchain_component_linker] = type;
	return true;
}

typedef bool((*toolchain_detect_cmd_arr_cb)(struct workspace *wk, obj comp, enum compiler_language lang, obj cmd_arr));

bool
toolchain_exe_detect(struct workspace *wk,
	const char *toolchain_exe_option_name,
	const char *exe_list[],
	obj comp,
	enum compiler_language lang,
	toolchain_detect_cmd_arr_cb cb)
{
	if (!toolchain_exe_option_name) {
		return false;
	}

	obj cmd_arr_opt;
	get_option(wk, NULL, &WKSTR(toolchain_exe_option_name), &cmd_arr_opt);
	struct obj_option *cmd_arr = get_obj_option(wk, cmd_arr_opt);

	if (cmd_arr->source > option_value_source_default) {
		return cb(wk, comp, lang, cmd_arr->val);
	}

	uint32_t i;
	for (i = 0; exe_list[i]; ++i) {
		obj cmd_arr;
		make_obj(wk, &cmd_arr, obj_array);
		obj_array_push(wk, cmd_arr, make_str(wk, exe_list[i]));

		if (cb(wk, comp, lang, cmd_arr)) {
			return true;
		}
	}

	return false;
}

static bool
toolchain_linker_detect(struct workspace *wk, obj comp, enum compiler_language lang)
{
	struct obj_compiler *compiler = get_obj_compiler(wk, comp);

	enum linker_type t = compilers[compiler->type[toolchain_component_compiler]].default_linker;
	if (host_machine.sys == machine_system_windows) {
		if (compiler->type[toolchain_component_compiler] == compiler_clang) {
			t = linker_lld_link;
		}
	}

	const char *exe_list[] = { linker_type_to_s(t), "ld", NULL };

	return toolchain_exe_detect(wk, "env.LD", exe_list, comp, lang, linker_detect);
}

static bool
toolchain_static_linker_detect(struct workspace *wk, obj comp, enum compiler_language lang)
{
	const char **exe_list = NULL;

	struct obj_compiler *compiler = get_obj_compiler(wk, comp);

	if (compiler->type[toolchain_component_compiler] == compiler_msvc) {
		static const char *msvc_list[] = { "lib", NULL };
		exe_list = msvc_list;
	} else {
		static const char *default_list[] = { "ar", "llvm-ar", NULL };
		exe_list = default_list;
	}

	return toolchain_exe_detect(wk, "env.AR", exe_list, comp, lang, static_linker_detect);
}

static bool
toolchain_compiler_detect(struct workspace *wk, obj comp, enum compiler_language lang)
{
	static const char *compiler_option[compiler_language_count] = {
		[compiler_language_c] = "env.CC",
		[compiler_language_cpp] = "env.CXX",
		[compiler_language_objc] = "env.OBJC",
		[compiler_language_objcpp] = "env.OBJCPP",
		[compiler_language_nasm] = "env.NASM",
	};

	const char **exe_list = NULL;

	if (host_machine.sys == machine_system_windows) {
		static const char *default_executables[][compiler_language_count] = {
			[compiler_language_c] = { "cl", "cc", "gcc", "clang", "clang-cl", NULL },
			[compiler_language_cpp] = { "cl", "c++", "g++", "clang++", "clang-cl", NULL },
			[compiler_language_objc] = { "cc", "gcc", NULL },
			[compiler_language_objcpp] = { "c++", "g++", "clang++", "clang-cl", NULL },
			[compiler_language_nasm] = { "nasm", "yasm", NULL },
		};

		exe_list = default_executables[lang];
	} else {
		static const char *default_executables[][compiler_language_count] = {
			[compiler_language_c] = { "cc", "gcc", "clang", NULL },
			[compiler_language_cpp] = { "c++", "g++", "clang++", NULL },
			[compiler_language_objc] = { "cc", "gcc", "clang", NULL },
			[compiler_language_objcpp] = { "c++", "g++", "clang++", NULL },
			[compiler_language_nasm] = { "nasm", "yasm", NULL },
		};

		exe_list = default_executables[lang];
	}

	return toolchain_exe_detect(wk, compiler_option[lang], exe_list, comp, lang, compiler_detect_cmd_arr);
}

bool
toolchain_detect(struct workspace *wk, obj *comp, enum compiler_language lang)
{
	make_obj(wk, comp, obj_compiler);

	if (!toolchain_compiler_detect(wk, *comp, lang)) {
		LOG_E("failed to detect compiler");
		return false;
	}

	if (!toolchain_linker_detect(wk, *comp, lang)) {
		LOG_E("failed to detect linker");
		return false;
	}

	if (!toolchain_static_linker_detect(wk, *comp, lang)) {
		LOG_E("failed to detect static linker");
		return false;
	}

	struct obj_compiler *compiler = get_obj_compiler(wk, *comp);

	LLOG_I("detected compiler %s ", compiler_type_to_s(compiler->type[toolchain_component_compiler]));
	obj_fprintf(wk,
		log_file(),
		"%o (%o), linker: %s (%o), static_linker: %s (%o)\n",
		compiler->ver,
		compiler->cmd_arr,
		linker_type_to_s(compiler->type[toolchain_component_linker]),
		compiler->linker_cmd_arr,
		static_linker_type_to_s(compiler->type[toolchain_component_static_linker]),
		compiler->static_linker_cmd_arr);

	return true;
}

/*******************************************************************************
 * Toolchain args
 ******************************************************************************/

#define TOOLCHAIN_ARGS(...)                      \
	static const char *argv[] = __VA_ARGS__; \
	static struct args args = { .args = argv, .len = ARRAY_LEN(argv) };

#define TOOLCHAIN_ARGS_RETURN static const struct args *
#define TOOLCHAIN_PROTO_0(name) TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_0)
#define TOOLCHAIN_PROTO_1i(name) TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1i)
#define TOOLCHAIN_PROTO_1s(name) TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1s)
#define TOOLCHAIN_PROTO_2s(name) TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_2s)
#define TOOLCHAIN_PROTO_1s1b(name) TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_1s1b)
#define TOOLCHAIN_PROTO_ns(name) TOOLCHAIN_ARGS_RETURN name(TOOLCHAIN_SIG_ns)

/* empty functions */

TOOLCHAIN_PROTO_0(toolchain_arg_empty_0)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_1i(toolchain_arg_empty_1i)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_1s(toolchain_arg_empty_1s)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_2s(toolchain_arg_empty_2s)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_1s1b(toolchain_arg_empty_1s1b)
{
	TOOLCHAIN_ARGS({ NULL });
	args.len = 0;
	return &args;
}

TOOLCHAIN_PROTO_ns(toolchain_arg_empty_ns)
{
	return a;
}

/* posix compilers */

TOOLCHAIN_PROTO_0(compiler_posix_args_object_extension)
{
	TOOLCHAIN_ARGS({ ".o" });

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_posix_args_compile_only)
{
	TOOLCHAIN_ARGS({ "-c" });

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_posix_args_preprocess_only)
{
	TOOLCHAIN_ARGS({ "-E" });

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_posix_args_output)
{
	TOOLCHAIN_ARGS({ "-o", NULL });

	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_1i(compiler_posix_args_optimization)
{
	TOOLCHAIN_ARGS({ NULL });

	switch ((enum compiler_optimization_lvl)a) {
	case compiler_optimization_lvl_0: argv[0] = "-O0"; break;
	case compiler_optimization_lvl_1:
	case compiler_optimization_lvl_2:
	case compiler_optimization_lvl_3: argv[0] = "-O1"; break;
	case compiler_optimization_lvl_none:
	case compiler_optimization_lvl_g:
	case compiler_optimization_lvl_s: args.len = 0; break;
	}

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_posix_args_debug)
{
	TOOLCHAIN_ARGS({ "-g" });

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_posix_args_include)
{
	TOOLCHAIN_ARGS({ "-I", NULL });

	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_posix_args_define)
{
	TOOLCHAIN_ARGS({ "-D", NULL });

	argv[1] = a;

	return &args;
}

/* gcc compilers */

TOOLCHAIN_PROTO_ns(linker_args_passthrough)
{
	static char buf[BUF_SIZE_S], buf2[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf, NULL, buf2 });

	if (a->len == 0) {
		return a;
	} else if (a->len == 1) {
		const char *passthrough_blacklist[] = {
			"-shared",
			"-bundle",
			"-dynamiclib",
		};

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(passthrough_blacklist); ++i) {
			if (strcmp(passthrough_blacklist[i], a->args[0]) == 0) {
				return a;
			}
		}

		snprintf(buf, BUF_SIZE_S, "-Wl,%s", a->args[0]);
		args.len = 1;
	} else if (a->len == 2) {
		snprintf(buf, BUF_SIZE_S, "-Wl,%s,%s", a->args[0], a->args[1]);
		args.len = 1;
	} else if (a->len == 3) {
		snprintf(buf, BUF_SIZE_S, "-Wl,%s", a->args[0]);
		argv[1] = a->args[1];
		snprintf(buf2, BUF_SIZE_S, "-Wl,%s", a->args[2]);
		args.len = 3;
	} else {
		UNREACHABLE;
	}

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_gcc_args_preprocess_only)
{
	TOOLCHAIN_ARGS({ "-E", "-P" });

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_gcc_args_include_system)
{
	TOOLCHAIN_ARGS({ "-isystem", NULL });

	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_2s(compiler_gcc_args_deps)
{
	TOOLCHAIN_ARGS({ "-MD", "-MQ", NULL, "-MF", NULL });

	argv[2] = a;
	argv[4] = b;

	return &args;
}

TOOLCHAIN_PROTO_1i(compiler_gcc_args_optimization)
{
	TOOLCHAIN_ARGS({ NULL });

	switch ((enum compiler_optimization_lvl)a) {
	case compiler_optimization_lvl_none: args.len = 0; break;
	case compiler_optimization_lvl_0: argv[0] = "-O0"; break;
	case compiler_optimization_lvl_1: argv[0] = "-O1"; break;
	case compiler_optimization_lvl_2: argv[0] = "-O2"; break;
	case compiler_optimization_lvl_3: argv[0] = "-O3"; break;
	case compiler_optimization_lvl_g: argv[0] = "-Og"; break;
	case compiler_optimization_lvl_s: argv[0] = "-Os"; break;
	}

	return &args;
}

TOOLCHAIN_PROTO_1i(compiler_gcc_args_warning_lvl)
{
	TOOLCHAIN_ARGS({ NULL, NULL, NULL });

	args.len = 0;

	switch ((enum compiler_warning_lvl)a) {
	case compiler_warning_lvl_everything: UNREACHABLE; break;
	case compiler_warning_lvl_3: argv[args.len] = "-Wpedantic"; ++args.len;
	/* fallthrough */
	case compiler_warning_lvl_2: argv[args.len] = "-Wextra"; ++args.len;
	/* fallthrough */
	case compiler_warning_lvl_1: argv[args.len] = "-Wall"; ++args.len;
	/* fallthrough */
	case compiler_warning_lvl_0: break;
	}

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_gcc_args_werror)
{
	TOOLCHAIN_ARGS({ "-Werror" });
	return &args;
}

TOOLCHAIN_PROTO_0(compiler_gcc_args_warn_everything)
{
	TOOLCHAIN_ARGS({ "-Wall",
		"-Winvalid-pch",
		"-Wextra",
		"-Wpedantic",
		"-Wcast-qual",
		"-Wconversion",
		"-Wfloat-equal",
		"-Wformat=2",
		"-Winline",
		"-Wmissing-declarations",
		"-Wredundant-decls",
		"-Wshadow",
		"-Wundef",
		"-Wuninitialized",
		"-Wwrite-strings",
		"-Wdisabled-optimization",
		"-Wpacked",
		"-Wpadded",
		"-Wmultichar",
		"-Wswitch-default",
		"-Wswitch-enum",
		"-Wunused-macros",
		"-Wmissing-include-dirs",
		"-Wunsafe-loop-optimizations",
		"-Wstack-protector",
		"-Wstrict-overflow=5",
		"-Warray-bounds=2",
		"-Wlogical-op",
		"-Wstrict-aliasing=3",
		"-Wvla",
		"-Wdouble-promotion",
		"-Wsuggest-attribute=const",
		"-Wsuggest-attribute=noreturn",
		"-Wsuggest-attribute=pure",
		"-Wtrampolines",
		"-Wvector-operation-performance",
		"-Wsuggest-attribute=format",
		"-Wdate-time",
		"-Wformat-signedness",
		"-Wnormalized=nfc",
		"-Wduplicated-cond",
		"-Wnull-dereference",
		"-Wshift-negative-value",
		"-Wshift-overflow=2",
		"-Wunused-const-variable=2",
		"-Walloca",
		"-Walloc-zero",
		"-Wformat-overflow=2",
		"-Wformat-truncation=2",
		"-Wstringop-overflow=3",
		"-Wduplicated-branches",
		"-Wattribute-alias=2",
		"-Wcast-align=strict",
		"-Wsuggest-attribute=cold",
		"-Wsuggest-attribute=malloc",
		"-Wanalyzer-too-complex",
		"-Warith-conversion",
		"-Wbidi-chars=ucn",
		"-Wopenacc-parallelism",
		"-Wtrivial-auto-var-init",
		"-Wbad-function-cast",
		"-Wmissing-prototypes",
		"-Wnested-externs",
		"-Wstrict-prototypes",
		"-Wold-style-definition",
		"-Winit-self",
		"-Wc++-compat",
		"-Wunsuffixed-float-constants" });
	return &args;
}

TOOLCHAIN_PROTO_0(compiler_clang_args_warn_everything)
{
	TOOLCHAIN_ARGS({ "-Weverything" });
	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_gcc_args_set_std)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-std=%s", a);

	return &args;
}

TOOLCHAIN_PROTO_1i(compiler_gcc_args_pgo)
{
	TOOLCHAIN_ARGS({ NULL, NULL });

	args.len = 1;

	switch ((enum compiler_pgo_stage)a) {
	case compiler_pgo_generate: argv[0] = "-fprofile-generate"; break;
	case compiler_pgo_use:
		argv[1] = "-fprofile-correction";
		++args.len;
		argv[0] = "-fprofile-use";
		break;
	}

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_gcc_args_pic)
{
	TOOLCHAIN_ARGS({ "-fPIC" });

	if (host_machine.is_windows) {
		args.len = 0;
	} else {
		args.len = 1;
	}
	return &args;
}

TOOLCHAIN_PROTO_0(compiler_gcc_args_pie)
{
	TOOLCHAIN_ARGS({ "-fPIE" });
	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_gcc_args_sanitize)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-fsanitize=%s", a);

	return &args;
}

TOOLCHAIN_PROTO_1i(compiler_gcc_args_visibility)
{
	TOOLCHAIN_ARGS({ NULL, NULL });

	args.len = 1;

	switch ((enum compiler_visibility_type)a) {
	case compiler_visibility_default: argv[0] = "-fvisibility=default"; break;
	case compiler_visibility_internal: argv[0] = "-fvisibility=internal"; break;
	case compiler_visibility_protected: argv[0] = "-fvisibility=protected"; break;
	case compiler_visibility_inlineshidden: argv[1] = "-fvisibility-inlines-hidden"; ++args.len;
	// fallthrough
	case compiler_visibility_hidden: argv[0] = "-fvisibility=hidden"; break;
	default: assert(false && "unreachable");
	}

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_gcc_args_specify_lang)
{
	TOOLCHAIN_ARGS({
		"-x",
		NULL,
	});

	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_gcc_args_color_output)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-fdiagnostics-color=%s", a);

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_gcc_args_lto)
{
	TOOLCHAIN_ARGS({ "-flto" });

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_gcc_args_coverage)
{
	TOOLCHAIN_ARGS({ "--coverage" })

	return &args;
}

/* cl compilers
 * see mesonbuild/compilers/mixins/visualstudio.py for reference
 */

TOOLCHAIN_PROTO_0(compiler_cl_args_always)
{
	TOOLCHAIN_ARGS({ "/nologo" });

	return &args;
}

TOOLCHAIN_PROTO_1s1b(compiler_cl_args_crt)
{
	TOOLCHAIN_ARGS({ NULL });

	if (strcmp(a, "from_buildtype") == 0) {
		argv[0] = b ? "/MDd" : "/MD";
	} else if (strcmp(a, "static_from_buildtype") == 0) {
		argv[0] = b ? "/MTd" : "/MT";
	} else {
		argv[0] = a;
	}

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_cl_args_debugfile)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "/Fd%s.pdb", a);

	return &args;
}

TOOLCHAIN_PROTO_2s(compiler_cl_args_deps)
{
	TOOLCHAIN_ARGS({ "/showIncludes" });

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_cl_args_compile_only)
{
	TOOLCHAIN_ARGS({ "/c" });

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_cl_args_preprocess_only)
{
	TOOLCHAIN_ARGS({ "/EP" });

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_cl_args_output)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });

	if (str_endswithi(&WKSTR(a), &WKSTR(".exe"))) {
		snprintf(buf, BUF_SIZE_S, "/Fe%s", a);
	} else {
		snprintf(buf, BUF_SIZE_S, "/Fo%s", a);
	}

	return &args;
}

TOOLCHAIN_PROTO_1i(compiler_cl_args_optimization)
{
	TOOLCHAIN_ARGS({ NULL, NULL });
	switch ((enum compiler_optimization_lvl)a) {
	case compiler_optimization_lvl_none:
	case compiler_optimization_lvl_g: args.len = 0; break;
	case compiler_optimization_lvl_0:
		args.len = 1;
		argv[0] = "/Od";
		break;
	case compiler_optimization_lvl_1:
		args.len = 1;
		argv[0] = "/O1";
		break;
	case compiler_optimization_lvl_2:
		args.len = 1;
		argv[0] = "/O2";
		break;
	case compiler_optimization_lvl_3:
		args.len = 2;
		argv[0] = "/O2";
		argv[1] = "/Gw";
		break;
	case compiler_optimization_lvl_s:
		args.len = 2;
		argv[0] = "/O1";
		argv[1] = "/Gw";
		break;
	}

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_cl_args_debug)
{
	TOOLCHAIN_ARGS({ "/Zi" });

	return &args;
}

TOOLCHAIN_PROTO_1i(compiler_cl_args_warning_lvl)
{
	/* meson uses nothing instead of /W0, but it's the same warning level
	 * see: https://mesonbuild.com/Builtin-options.html#details-for-warning_level
	 */

	TOOLCHAIN_ARGS({ NULL });

	switch ((enum compiler_warning_lvl)a) {
	case compiler_warning_lvl_0: args.len = 0; break;
	case compiler_warning_lvl_1: argv[0] = "/W2"; break;
	case compiler_warning_lvl_2: argv[0] = "/W3"; break;
	case compiler_warning_lvl_everything:
	case compiler_warning_lvl_3: argv[0] = "/W4"; break;
	}

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_cl_args_warn_everything)
{
	TOOLCHAIN_ARGS({ "/Wall" });
	return &args;
}

TOOLCHAIN_PROTO_0(compiler_cl_args_werror)
{
	TOOLCHAIN_ARGS({ "/WX" });
	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_cl_args_set_std)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });
	if (strcmp(a, "c11") == 0) {
		memcpy(buf, "/std:c11", sizeof("/std:c11"));
	} else if (strcmp(a, "c17") == 0 || strcmp(a, "c18") == 0) {
		memcpy(buf, "/std:c17", sizeof("/std:c17"));
	} else {
		args.len = 0;
	}

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_cl_args_include)
{
	TOOLCHAIN_ARGS({ "/I", NULL });

	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_cl_args_sanitize)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });
	snprintf(buf, BUF_SIZE_S, "-fsanitize=%s", a);

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_cl_args_define)
{
	TOOLCHAIN_ARGS({ "/D", NULL });

	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_cl_args_object_extension)
{
	TOOLCHAIN_ARGS({ ".obj" });

	return &args;
}

TOOLCHAIN_PROTO_1s(compiler_cl_args_std_supported)
{
	const char *supported[] = {
		"c++14",
		"c++17",
		"c++20",
		"c++latest",
		"c11",
		"c17",
		"clatest",
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(supported); ++i) {
		if (strcmp(a, supported[i]) == 0) {
			return TOOLCHAIN_TRUE;
		}
	}

	return TOOLCHAIN_FALSE;
}

TOOLCHAIN_PROTO_1s(compiler_clang_cl_args_color_output)
{
	TOOLCHAIN_ARGS({ "-fcolor-diagnostics" });

	return &args;
	(void)a;
}

TOOLCHAIN_PROTO_0(compiler_clang_cl_args_lto)
{
	TOOLCHAIN_ARGS({ "-flto" });

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_deps_gcc)
{
	TOOLCHAIN_ARGS({ "gcc" });

	return &args;
}

TOOLCHAIN_PROTO_0(compiler_deps_msvc)
{
	TOOLCHAIN_ARGS({ "msvc" });

	return &args;
}

TOOLCHAIN_PROTO_1s(linker_posix_args_lib)
{
	TOOLCHAIN_ARGS({ "-l", NULL });
	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_2s(linker_posix_args_input_output)
{
	TOOLCHAIN_ARGS({ NULL, NULL });
	argv[0] = b;
	argv[1] = a;
	return &args;
}

/* technically not a posix linker argument, but include it here since it is so
 * common
 */

TOOLCHAIN_PROTO_0(linker_posix_args_shared)
{
	TOOLCHAIN_ARGS({ "-shared" });
	return &args;
}

TOOLCHAIN_PROTO_0(linker_ld_args_as_needed)
{
	TOOLCHAIN_ARGS({ "--as-needed" });
	return &args;
}

TOOLCHAIN_PROTO_0(linker_ld_args_no_undefined)
{
	TOOLCHAIN_ARGS({ "--no-undefined" });
	return &args;
}

TOOLCHAIN_PROTO_0(linker_ld_args_start_group)
{
	TOOLCHAIN_ARGS({ "--start-group" });
	return &args;
}

TOOLCHAIN_PROTO_0(linker_ld_args_end_group)
{
	TOOLCHAIN_ARGS({ "--end-group" });
	return &args;
}

TOOLCHAIN_PROTO_1s(linker_ld_args_soname)
{
	TOOLCHAIN_ARGS({ "-soname", NULL });
	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_1s(linker_ld_args_rpath)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-rpath,%s", a);

	return &args;
}

TOOLCHAIN_PROTO_0(linker_ld_args_allow_shlib_undefined)
{
	TOOLCHAIN_ARGS({ "--allow-shlib-undefined" });
	return &args;
}

TOOLCHAIN_PROTO_0(linker_ld_args_export_dynamic)
{
	TOOLCHAIN_ARGS({ "-export-dynamic" });
	return &args;
}

TOOLCHAIN_PROTO_0(linker_ld_args_fatal_warnings)
{
	TOOLCHAIN_ARGS({ "--fatal-warnings" });
	return &args;
}

TOOLCHAIN_PROTO_1s(linker_ld_args_whole_archive)
{
	TOOLCHAIN_ARGS({ "--whole-archive", NULL, "--no-whole-archive" });
	argv[1] = a;
	return &args;
}

/* cl linkers */

TOOLCHAIN_PROTO_1s(linker_link_args_lib)
{
	TOOLCHAIN_ARGS({ NULL });
	argv[0] = a;

	return &args;
}

TOOLCHAIN_PROTO_0(linker_link_args_debug)
{
	TOOLCHAIN_ARGS({ "/DEBUG" });
	return &args;
}

TOOLCHAIN_PROTO_0(linker_link_args_shared)
{
	TOOLCHAIN_ARGS({ "/DLL" });
	return &args;
}

TOOLCHAIN_PROTO_1s(linker_link_args_soname)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "/OUT:%s", a);

	return &args;
}

TOOLCHAIN_PROTO_2s(linker_link_args_input_output)
{
	static char buf[BUF_SIZE_S];
	TOOLCHAIN_ARGS({ buf, NULL });

	snprintf(buf, BUF_SIZE_S, "/out:%s", b);

	argv[1] = a;

	return &args;
}

TOOLCHAIN_PROTO_0(linker_link_args_always)
{
	TOOLCHAIN_ARGS({ "/NOLOGO", NULL });

	argv[1] = host_machine.address_bits == 64 ? "/MACHINE:X64" : "/MACHINE:X86";

	return &args;
}

TOOLCHAIN_PROTO_1s(linker_lld_link_args_whole_archive)
{
	TOOLCHAIN_ARGS({ "/whole-archive", NULL });
	argv[1] = a;
	return &args;
}

/* apple linker */

TOOLCHAIN_PROTO_1s(linker_apple_args_whole_archive)
{
	TOOLCHAIN_ARGS({ "-force_load", NULL });
	argv[1] = a;
	return &args;
}

TOOLCHAIN_PROTO_0(linker_apple_args_allow_shlib_undefined)
{
	TOOLCHAIN_ARGS({ "-undefined", "dynamic_lookup" });
	return &args;
}

TOOLCHAIN_PROTO_0(linker_apple_args_shared_module)
{
	TOOLCHAIN_ARGS({ "-bundle" });
	return &args;
}

/* static linkers */

TOOLCHAIN_PROTO_0(static_linker_ar_posix_args_base)
{
	TOOLCHAIN_ARGS({ "csr" });
	return &args;
}

TOOLCHAIN_PROTO_0(static_linker_ar_gcc_args_base)
{
	TOOLCHAIN_ARGS({ "csrD" });
	return &args;
}

struct compiler compilers[compiler_type_count];
struct linker linkers[linker_type_count];
struct static_linker static_linkers[static_linker_type_count];

const struct language languages[compiler_language_count] = {
	[compiler_language_null] = { 0 },
	[compiler_language_c] = { .is_header = false },
	[compiler_language_c_hdr] = { .is_header = true },
	[compiler_language_cpp] = { .is_header = false },
	[compiler_language_cpp_hdr] = { .is_header = true },
	[compiler_language_c_obj] = { .is_linkable = true },
	[compiler_language_assembly] = { 0 },
	[compiler_language_llvm_ir] = { 0 },
};

#define TOOLCHAIN_ARG_MEMBER_(name, __type, params, names) .name = toolchain_arg_empty_##__type,
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, type)

static void
build_compilers(void)
{
	struct compiler empty = {
		.args = { FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER) },
	};

	empty.args.object_ext = compiler_posix_args_object_extension;

	struct compiler clang_llvm_ir = empty;
	clang_llvm_ir.args.compile_only = compiler_posix_args_compile_only;
	clang_llvm_ir.args.output = compiler_posix_args_output;

	struct compiler posix = empty;
	posix.args.compile_only = compiler_posix_args_compile_only;
	posix.args.preprocess_only = compiler_posix_args_preprocess_only;
	posix.args.output = compiler_posix_args_output;
	posix.args.optimization = compiler_posix_args_optimization;
	posix.args.debug = compiler_posix_args_debug;
	posix.args.include = compiler_posix_args_include;
	posix.args.include_system = compiler_posix_args_include;
	posix.args.define = compiler_posix_args_define;
	posix.default_linker = linker_posix;
	posix.default_static_linker = static_linker_ar_posix;

	struct compiler gcc = posix;
	gcc.args.linker_passthrough = linker_args_passthrough;
	gcc.args.preprocess_only = compiler_gcc_args_preprocess_only;
	gcc.args.deps = compiler_gcc_args_deps;
	gcc.args.optimization = compiler_gcc_args_optimization;
	gcc.args.warning_lvl = compiler_gcc_args_warning_lvl;
	gcc.args.warn_everything = compiler_gcc_args_warn_everything;
	gcc.args.werror = compiler_gcc_args_werror;
	gcc.args.set_std = compiler_gcc_args_set_std;
	gcc.args.include_system = compiler_gcc_args_include_system;
	gcc.args.pgo = compiler_gcc_args_pgo;
	gcc.args.pic = compiler_gcc_args_pic;
	gcc.args.pie = compiler_gcc_args_pie;
	gcc.args.sanitize = compiler_gcc_args_sanitize;
	gcc.args.visibility = compiler_gcc_args_visibility;
	gcc.args.specify_lang = compiler_gcc_args_specify_lang;
	gcc.args.color_output = compiler_gcc_args_color_output;
	gcc.args.enable_lto = compiler_gcc_args_lto;
	gcc.args.deps_type = compiler_deps_gcc;
	gcc.args.coverage = compiler_gcc_args_coverage;
	gcc.default_linker = linker_ld;
	gcc.default_static_linker = static_linker_ar_gcc;

	struct compiler clang = gcc;
	clang.args.warn_everything = compiler_clang_args_warn_everything;
	clang.default_linker = linker_clang;

	struct compiler apple_clang = clang;
	apple_clang.default_linker = linker_apple;
	apple_clang.default_static_linker = static_linker_ar_posix;

	struct compiler msvc = empty;
	msvc.args.deps = compiler_cl_args_deps;
	msvc.args.compile_only = compiler_cl_args_compile_only;
	msvc.args.preprocess_only = compiler_cl_args_preprocess_only;
	msvc.args.output = compiler_cl_args_output;
	msvc.args.optimization = compiler_cl_args_optimization;
	msvc.args.debug = compiler_cl_args_debug;
	msvc.args.warning_lvl = compiler_cl_args_warning_lvl;
	msvc.args.warn_everything = compiler_cl_args_warn_everything;
	msvc.args.werror = compiler_cl_args_werror;
	msvc.args.set_std = compiler_cl_args_set_std;
	msvc.args.include = compiler_cl_args_include;
	msvc.args.sanitize = compiler_cl_args_sanitize;
	msvc.args.define = compiler_cl_args_define;
	msvc.args.always = compiler_cl_args_always;
	msvc.args.crt = compiler_cl_args_crt;
	msvc.args.debugfile = compiler_cl_args_debugfile;
	msvc.default_linker = linker_msvc;
	msvc.default_static_linker = static_linker_msvc;
	msvc.args.object_ext = compiler_cl_args_object_extension;
	msvc.args.deps_type = compiler_deps_msvc;
	msvc.args.std_supported = compiler_cl_args_std_supported;

	struct compiler clang_cl = msvc;
	clang_cl.args.color_output = compiler_clang_cl_args_color_output;
	clang_cl.args.enable_lto = compiler_clang_cl_args_lto;
	clang_cl.default_linker = linker_lld_link;

	compilers[compiler_posix] = posix;
	compilers[compiler_gcc] = gcc;
	compilers[compiler_clang] = clang;
	compilers[compiler_apple_clang] = apple_clang;
	compilers[compiler_clang_llvm_ir] = clang_llvm_ir;
	compilers[compiler_clang_cl] = clang_cl;
	compilers[compiler_msvc] = msvc;

	struct compiler nasm = empty;
	nasm.args.output = compiler_posix_args_output;
	nasm.args.optimization = compiler_posix_args_optimization;
	nasm.args.debug = compiler_posix_args_debug;
	nasm.args.include = compiler_posix_args_include;
	nasm.args.include_system = compiler_posix_args_include;
	nasm.args.define = compiler_posix_args_define;
	nasm.default_linker = linker_posix;
	nasm.default_static_linker = static_linker_ar_posix;

	compilers[compiler_nasm] = nasm;
	compilers[compiler_yasm] = nasm;
}

static void
build_linkers(void)
{
	/* linkers */
	struct linker empty = { .args = { FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) } };

	struct linker posix = empty;
	posix.args.lib = linker_posix_args_lib;
	posix.args.shared = linker_posix_args_shared;
	posix.args.input_output = linker_posix_args_input_output;

	struct linker ld = posix;
	ld.args.as_needed = linker_ld_args_as_needed;
	ld.args.no_undefined = linker_ld_args_no_undefined;
	ld.args.start_group = linker_ld_args_start_group;
	ld.args.end_group = linker_ld_args_end_group;
	ld.args.soname = linker_ld_args_soname;
	ld.args.rpath = linker_ld_args_rpath;
	ld.args.pgo = compiler_gcc_args_pgo;
	ld.args.sanitize = compiler_gcc_args_sanitize;
	ld.args.allow_shlib_undefined = linker_ld_args_allow_shlib_undefined;
	ld.args.shared_module = linker_posix_args_shared;
	ld.args.export_dynamic = linker_ld_args_export_dynamic;
	ld.args.fatal_warnings = linker_ld_args_fatal_warnings;
	ld.args.whole_archive = linker_ld_args_whole_archive;
	ld.args.enable_lto = compiler_gcc_args_lto;
	ld.args.coverage = compiler_gcc_args_coverage;

	struct linker lld = ld;

	struct linker apple = posix;
	posix.args.shared = linker_posix_args_shared;
	apple.args.sanitize = compiler_gcc_args_sanitize;
	apple.args.enable_lto = compiler_gcc_args_lto;
	apple.args.allow_shlib_undefined = linker_apple_args_allow_shlib_undefined;
	apple.args.shared_module = linker_apple_args_shared_module;
	apple.args.whole_archive = linker_apple_args_whole_archive;

	struct linker link = empty;
	link.args.lib = linker_link_args_lib;
	link.args.debug = linker_link_args_debug;
	link.args.shared = linker_link_args_shared;
	link.args.soname = linker_link_args_soname;
	link.args.input_output = linker_link_args_input_output;
	link.args.always = linker_link_args_always;

	struct linker lld_link = link;
	lld_link.args.whole_archive = linker_lld_link_args_whole_archive;
	lld_link.args.always = toolchain_arg_empty_0;

	linkers[linker_posix] = posix;
	linkers[linker_ld] = ld;
	linkers[linker_clang] = lld;
	linkers[linker_apple] = apple;
	linkers[linker_lld_link] = lld_link;
	linkers[linker_msvc] = link;
}

static void
build_static_linkers(void)
{
	struct static_linker empty = { .args = { FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) } };

	struct static_linker posix = empty;
	posix.args.base = static_linker_ar_posix_args_base;
	posix.args.input_output = linker_posix_args_input_output;

	struct static_linker gcc = posix;
	gcc.args.base = static_linker_ar_gcc_args_base;

	struct static_linker msvc = empty;
	msvc.args.input_output = linker_link_args_input_output;
	msvc.args.always = compiler_cl_args_always;

	static_linkers[static_linker_ar_posix] = posix;
	static_linkers[static_linker_ar_gcc] = gcc;
	static_linkers[static_linker_msvc] = msvc;
}

#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

void
compilers_init(void)
{
	build_compilers();
	build_linkers();
	build_static_linkers();
}

#define TOOLCHAIN_ARG_MEMBER_(name, comp, __type, params, names) { #name, toolchain_arg_arity_##__type },
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, comp, type)
static struct toolchain_arg_handler toolchain_compiler_arg_handlers[] = { FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER) };
static struct toolchain_arg_handler toolchain_linker_arg_handlers[] = { FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) };
static struct toolchain_arg_handler toolchain_static_linker_arg_handlers[]
	= { FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) };
#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

static struct {
	struct toolchain_arg_handler *handlers;
	uint32_t len;
} toolchain_arg_handlers[] = {
	[toolchain_component_compiler]
	= { toolchain_compiler_arg_handlers, ARRAY_LEN(toolchain_compiler_arg_handlers) },
	[toolchain_component_linker] = { toolchain_linker_arg_handlers, ARRAY_LEN(toolchain_linker_arg_handlers) },
	[toolchain_component_static_linker]
	= { toolchain_static_linker_arg_handlers, ARRAY_LEN(toolchain_static_linker_arg_handlers) },
};

const struct toolchain_arg_handler *
get_toolchain_arg_handler_info(enum toolchain_component component, const char *name)
{
	uint32_t i;
	for (i = 0; i < toolchain_arg_handlers[component].len; ++i) {
		if (strcmp(toolchain_arg_handlers[component].handlers[i].name, name) == 0) {
			return &toolchain_arg_handlers[component].handlers[i];
		}
	}

	return 0;
}

void
toolchain_arg_arity_to_sig(enum toolchain_arg_arity arity, type_tag signature[2], uint32_t *len)
{
	switch (arity) {
	case toolchain_arg_arity_0: {
		*len = 0;
		break;
	}
	case toolchain_arg_arity_1i: {
		signature[0] = tc_number;
		*len = 1;
		break;
	}
	case toolchain_arg_arity_1s: {
		signature[0] = tc_string;
		*len = 1;
		break;
	}
	case toolchain_arg_arity_2s: {
		signature[0] = tc_string;
		signature[1] = tc_string;
		*len = 2;
		break;
	}
	case toolchain_arg_arity_1s1b: {
		signature[0] = tc_string;
		signature[1] = tc_bool;
		*len = 2;
		break;
	}
	case toolchain_arg_arity_ns: {
		signature[0] = TYPE_TAG_GLOB | tc_string;
		*len = 1;
		break;
	}
	}
}

static obj
lookup_toolchain_arg_override(struct workspace *wk,
	struct obj_compiler *c,
	enum toolchain_component component,
	uint32_t arg)
{
	obj overrides = c->overrides[component], override;
	if (overrides
		&& obj_dict_index_str(wk, overrides, toolchain_arg_handlers[component].handlers[arg].name, &override)) {
		return override;
	}

	return 0;
}

enum toolchain_arg_by_component {
#define TOOLCHAIN_ARG_MEMBER_(comp, _name) toolchain_arg_by_component_##comp##_name,
#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(comp, _##name)
	FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER) toolchain_arg_by_component_reset_0 = -1,
	FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER) toolchain_arg_by_component_reset_1 = -1,
	FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_
};

static obj handle_toolchain_arg_override;

static const struct args *
handle_toolchain_arg_override_convert_to_args(struct workspace *wk, obj list)
{
	static const char *argv[32];
	static struct args args = { .args = argv, .len = 0 };

	obj v;
	obj_array_for(wk, list, v) {
		assert(args.len < ARRAY_LEN(argv) && "increase size of argv");

		++args.len;
		argv[args.len] = get_cstr(wk, v);
	}

	return &args;
}

static const struct args *
handle_toolchain_arg_override_check_const(struct workspace *wk)
{
	if (get_obj_type(wk, handle_toolchain_arg_override) == obj_array) {
		return handle_toolchain_arg_override_convert_to_args(wk, handle_toolchain_arg_override);
	}

	return 0;
}

#define constant_override_check()                                                        \
	{                                                                                \
		const struct args *args = handle_toolchain_arg_override_check_const(wk); \
		if (args) {                                                              \
			return args;                                                     \
		}                                                                        \
	}

static const struct args *
handle_toolchain_arg_override_0(TOOLCHAIN_SIG_0)
{
	constant_override_check();

	return 0;
}

static const struct args *
handle_toolchain_arg_override_1i(TOOLCHAIN_SIG_1i)
{
	constant_override_check();
	return 0;
}

static const struct args *
handle_toolchain_arg_override_1s(TOOLCHAIN_SIG_1s)
{
	constant_override_check();
	return 0;
}

static const struct args *
handle_toolchain_arg_override_2s(TOOLCHAIN_SIG_2s)
{
	constant_override_check();
	return 0;
}

static const struct args *
handle_toolchain_arg_override_1s1b(TOOLCHAIN_SIG_1s1b)
{
	constant_override_check();
	return 0;
}

static const struct args *
handle_toolchain_arg_override_ns(TOOLCHAIN_SIG_ns)
{
	constant_override_check();
	return 0;
}

#define TOOLCHAIN_ARG_MEMBER_(name, _name, component, _type, params, names)                                        \
	const struct args *toolchain_##component##_name params                                                     \
	{                                                                                                          \
		handle_toolchain_arg_override = lookup_toolchain_arg_override(                                     \
			wk, comp, toolchain_component_##component, toolchain_arg_by_component_##component##_name); \
		if (handle_toolchain_arg_override) {                                                               \
			return handle_toolchain_arg_override_##_type names;                                        \
		}                                                                                                  \
                                                                                                                   \
		return component##s[comp->type[toolchain_component_##component]].args.name names;                  \
	}

#define TOOLCHAIN_ARG_MEMBER(name, comp, type) TOOLCHAIN_ARG_MEMBER_(name, _##name, comp, type)

FOREACH_COMPILER_ARG(TOOLCHAIN_ARG_MEMBER)
FOREACH_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)
FOREACH_STATIC_LINKER_ARG(TOOLCHAIN_ARG_MEMBER)

#undef TOOLCHAIN_ARG_MEMBER
#undef TOOLCHAIN_ARG_MEMBER_

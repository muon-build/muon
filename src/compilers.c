#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "buf_size.h"
#include "compilers.h"
#include "guess.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/run_cmd.h"

const char *
compiler_type_to_s(enum compiler_type t)
{
	switch (t) {
	case compiler_posix: return "posix";
	case compiler_gcc: return "gcc";
	case compiler_clang: return "clang";
	default: assert(false); return "";
	}
}

static const char *compiler_language_names[compiler_language_count] = {
	[compiler_language_c] = "c",
	[compiler_language_c_hdr] = "c_hdr",
	[compiler_language_cpp] = "cpp",
	[compiler_language_cpp_hdr] = "cpp_hdr",
	[compiler_language_c_obj] = "c_obj",
};

const char *
compiler_language_to_s(enum compiler_language l)
{
	assert(l < compiler_language_count);
	return compiler_language_names[l];
}

bool
s_to_compiler_language(const char *s, enum compiler_language *l)
{
	uint32_t i;
	for (i = 0; i < compiler_language_count; ++i) {
		if (strcmp(s, compiler_language_names[i]) == 0) {
			*l = i;
			return true;
		}
	}

	return false;
}

bool
filename_to_compiler_language(const char *str, enum compiler_language *l)
{
	static const char *exts[][10] = {
		[compiler_language_c] = { "c" },
		[compiler_language_c_hdr] = { "h" },
		[compiler_language_cpp] = { "cc", "cpp", },
		[compiler_language_cpp_hdr] = { "hpp" },
		[compiler_language_c_obj] = { "o" },
	};
	uint32_t i, j;
	const char *ext;

	if (!(ext = strrchr(str, '.'))) {
		return false;
	}
	++ext;

	for (i = 0; i < compiler_language_count; ++i) {
		for (j = 0; exts[i][j]; ++j) {
			if (strcmp(ext, exts[i][j]) == 0) {
				*l = i;
				return true;
			}
		}
	}

	return false;
}

static bool
compiler_detect_c_or_cpp(struct workspace *wk, const char *cc, uint32_t *comp_id)
{
	// helpful: mesonbuild/compilers/detect.py:350
	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd(&cmd_ctx, cc, (const char *[]){
		(char *)cc, "--version", NULL,
	}, NULL)) {
		run_cmd_ctx_destroy(&cmd_ctx);
		return false;
	}

	if (cmd_ctx.status != 0) {
		cmd_ctx = (struct run_cmd_ctx) { 0 };
		if (!run_cmd(&cmd_ctx, cc, (const char *[]){
			(char *)cc, "-v", NULL,
		}, NULL)) {
			run_cmd_ctx_destroy(&cmd_ctx);
			return false;
		}
	}

	enum compiler_type type;
	bool unknown = true;
	obj ver;

	if (cmd_ctx.status != 0) {
		goto detection_over;
	}

	if (strstr(cmd_ctx.out.buf, "clang") || strstr(cmd_ctx.out.buf, "Clang")) {
		type = compiler_clang;
	} else if (strstr(cmd_ctx.out.buf, "Free Software Foundation")) {
		type = compiler_gcc;
	} else {
		goto detection_over;
	}

	char *p;
	if ((p = strchr(cmd_ctx.out.buf, '\n'))) {
		*p = 0;
	}

	if (!guess_version(wk, cmd_ctx.out.buf, &ver)) {
		ver = make_str(wk, "unknown");
	}

	unknown = false;
	LOG_I("detected compiler %s %s (%s)", compiler_type_to_s(type),
		get_cstr(wk, ver), cc);

detection_over:
	if (unknown) {
		LOG_W("unable to detect compiler type, falling back on posix compiler");
		type = compiler_posix;
		ver = make_str(wk, "unknown");
	}

	struct obj *comp = make_obj(wk, comp_id, obj_compiler);
	comp->dat.compiler.name = make_str(wk, cc);
	comp->dat.compiler.type = type;
	comp->dat.compiler.ver = ver;

	run_cmd_ctx_destroy(&cmd_ctx);
	return true;
}

bool
compiler_detect(struct workspace *wk, uint32_t *comp, enum compiler_language lang)
{
	const char *cmd;

	static const char *compiler_env_override[] = {
		[compiler_language_c] = "CC",
		[compiler_language_cpp] = "CXX",
	};

	static const char *compiler_default[] = {
		[compiler_language_c] = "cc",
		[compiler_language_cpp] = "c++",
	};

	if (!(cmd = getenv(compiler_env_override[lang]))) {
		cmd = compiler_default[lang];
	}

	switch (lang) {
	case compiler_language_c:
	case compiler_language_cpp:
		if (!compiler_detect_c_or_cpp(wk, cmd, comp)) {
			return false;
		}

		get_obj(wk, *comp)->dat.compiler.lang = lang;
		return true;
	default:
		assert(false);
		return false;
	}
}

#define COMPILER_ARGS(...) \
	static const char *argv[] = __VA_ARGS__; \
	static struct args args = { \
		.args = argv, \
		.len = ARRAY_LEN(argv) \
	};

/* posix compilers */

static const struct args *
compiler_posix_args_compile_only(void)
{
	COMPILER_ARGS({ "-c" });

	return &args;
}

static const struct args *
compiler_posix_args_preprocess_only(void)
{
	COMPILER_ARGS({ "-E" });

	return &args;
}

static const struct args *
compiler_posix_args_output(const char *f)
{
	COMPILER_ARGS({ "-o", NULL });

	argv[1] = f;

	return &args;
}

static const struct args *
compiler_posix_args_optimization(uint32_t lvl)
{
	COMPILER_ARGS({ NULL });

	switch ((enum compiler_optimization_lvl)lvl) {
	case compiler_optimization_lvl_0:
		argv[0] = "-O0";
		break;
	case compiler_optimization_lvl_1:
	case compiler_optimization_lvl_2:
	case compiler_optimization_lvl_3:
		argv[0] = "-O1";
		break;
	case compiler_optimization_lvl_g:
	case compiler_optimization_lvl_s:
		args.len = 0;
		break;
	}

	return &args;
}

static const struct args *
compiler_posix_args_debug(void)
{
	COMPILER_ARGS({ "-g" });

	return &args;
}

static const struct args *
compiler_posix_args_include(const char *dir)
{
	COMPILER_ARGS({ "-I", NULL });

	argv[1] = dir;

	return &args;
}

/* gcc compilers */

static const struct args *
compiler_gcc_args_include_system(const char *dir)
{
	COMPILER_ARGS({ "-isystem", NULL });

	argv[1] = dir;

	return &args;
}

static const struct args *
compiler_gcc_args_deps(const char *out_target, const char *out_file)
{
	COMPILER_ARGS({ "-MD", "-MQ", NULL, "-MF", NULL });

	argv[2] = out_target;
	argv[4] = out_file;

	return &args;
}

static const struct args *
compiler_gcc_args_optimization(uint32_t lvl)
{
	COMPILER_ARGS({ NULL });

	switch ((enum compiler_optimization_lvl)lvl) {
	case compiler_optimization_lvl_0:
		argv[0] = "-O0";
		break;
	case compiler_optimization_lvl_1:
		argv[0] = "-O1";
		break;
	case compiler_optimization_lvl_2:
		argv[0] = "-O2";
		break;
	case compiler_optimization_lvl_3:
		argv[0] = "-O3";
		break;
	case compiler_optimization_lvl_g:
		argv[0] = "-Og";
		break;
	case compiler_optimization_lvl_s:
		argv[0] = "-Os";
		break;
	}

	return &args;
}

static const struct args *
compiler_gcc_args_warning_lvl(uint32_t lvl)
{
	COMPILER_ARGS({ NULL, NULL, NULL });

	args.len = 0;

	switch ((enum compiler_warning_lvl)lvl) {
	case compiler_warning_lvl_3:
		argv[args.len] = "-Wpedantic";
		++args.len;
	/* fallthrough */
	case compiler_warning_lvl_2:
		argv[args.len] = "-Wextra";
		++args.len;
	/* fallthrough */
	case compiler_warning_lvl_1:
		argv[args.len] = "-Wall";
		++args.len;
	/* fallthrough */
	case compiler_warning_lvl_0:
		break;
	}

	return &args;
}

static const struct args *
compiler_gcc_args_werror(void)
{
	COMPILER_ARGS({ "-Werror" });
	return &args;
}

static const struct args *
compiler_gcc_args_set_std(const char *std)
{
	static char buf[BUF_SIZE_S];
	COMPILER_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-std=%s", std);

	return &args;
}

static const struct args *
compiler_gcc_args_pic(void)
{
	COMPILER_ARGS({ "-fpic" });
	return &args;
}

static const struct args *
compiler_gcc_args_sanitize(const char *sanitizers)
{
	static char buf[BUF_SIZE_S];
	COMPILER_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-fsanitize=%s", sanitizers);

	return &args;
}

compiler_get_arg_func_0 as_needed;
compiler_get_arg_func_0 no_undefined;
compiler_get_arg_func_0 start_group;
compiler_get_arg_func_0 end_group;

static const struct args *
compiler_arg_empty_0(void)
{
	COMPILER_ARGS({ NULL });
	args.len = 0;
	return &args;
}

static const struct args *
compiler_arg_empty_1i(uint32_t _)
{
	COMPILER_ARGS({ NULL });
	args.len = 0;
	return &args;
}

static const struct args *
compiler_arg_empty_1s(const char * _)
{
	COMPILER_ARGS({ NULL });
	args.len = 0;
	return &args;
}

static const struct args *
compiler_arg_empty_2s(const char *_, const char *__)
{
	COMPILER_ARGS({ NULL });
	args.len = 0;
	return &args;
}

struct compiler compilers[compiler_type_count];
struct linker linkers[linker_type_count];

const struct language languages[] = {
	[compiler_language_c] = { .is_header = false },
	[compiler_language_c_hdr] = { .is_header = true },
	[compiler_language_cpp] = { .is_header = false },
	[compiler_language_cpp_hdr] = { .is_header = true },
	[compiler_language_c_obj] = { .is_linkable = true },
};

static void
build_compilers(void)
{
	struct compiler empty = {
		.args = {
			.deps            = compiler_arg_empty_2s,
			.compile_only    = compiler_arg_empty_0,
			.preprocess_only = compiler_arg_empty_0,
			.output          = compiler_arg_empty_1s,
			.optimization    = compiler_arg_empty_1i,
			.debug           = compiler_arg_empty_0,
			.warning_lvl     = compiler_arg_empty_1i,
			.werror          = compiler_arg_empty_0,
			.set_std         = compiler_arg_empty_1s,
			.include         = compiler_arg_empty_1s,
			.include_system  = compiler_arg_empty_1s,
			.pic             = compiler_arg_empty_0,
			.sanitize        = compiler_arg_empty_1s,
		}
	};

	struct compiler posix = empty;
	posix.args.compile_only = compiler_posix_args_compile_only;
	posix.args.preprocess_only = compiler_posix_args_preprocess_only;
	posix.args.output = compiler_posix_args_output;
	posix.args.optimization = compiler_posix_args_optimization;
	posix.args.debug = compiler_posix_args_debug;
	posix.args.include = compiler_posix_args_include;
	posix.args.include_system = compiler_posix_args_include;
	posix.linker = linker_posix;

	struct compiler gcc = posix;
	gcc.args.deps = compiler_gcc_args_deps;
	gcc.args.optimization = compiler_gcc_args_optimization;
	gcc.args.warning_lvl = compiler_gcc_args_warning_lvl;
	gcc.args.werror = compiler_gcc_args_werror;
	gcc.args.set_std = compiler_gcc_args_set_std;
	gcc.args.include_system = compiler_gcc_args_include_system;
	gcc.args.pic = compiler_gcc_args_pic;
	gcc.args.sanitize = compiler_gcc_args_sanitize;
	gcc.deps = compiler_deps_gcc;
	gcc.linker = linker_gcc;

	compilers[compiler_posix] = posix;
	compilers[compiler_gcc] = gcc;
	compilers[compiler_clang] = gcc;

}

static const struct args *
linker_gcc_args_as_needed(void)
{
	COMPILER_ARGS({ "-Wl,--as-needed" });
	return &args;
}


static const struct args *
linker_gcc_args_no_undefined(void)
{
	COMPILER_ARGS({ "-Wl,--no-undefined" });
	return &args;
}


static const struct args *
linker_gcc_args_start_group(void)
{
	COMPILER_ARGS({ "-Wl,--start-group" });
	return &args;
}


static const struct args *
linker_gcc_args_end_group(void)
{
	COMPILER_ARGS({ "-Wl,--end-group" });
	return &args;
}

static const struct args *
linker_gcc_args_shared(void)
{
	COMPILER_ARGS({ "-shared" });
	return &args;
}

static const struct args *
linker_gcc_args_soname(const char *soname)
{
	static char buf[BUF_SIZE_S];
	COMPILER_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-Wl,-soname,%s", soname);

	return &args;
}

static const struct args *
linker_gcc_args_rpath(const char *rpath)
{
	static char buf[BUF_SIZE_S];
	COMPILER_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-Wl,-rpath,%s", rpath);

	return &args;
}

static void
build_linkers(void)
{
	/* linkers */
	struct linker empty = {
		.args = {
			.as_needed    = compiler_arg_empty_0,
			.no_undefined = compiler_arg_empty_0,
			.start_group  = compiler_arg_empty_0,
			.end_group    = compiler_arg_empty_0,
			.shared       = compiler_arg_empty_0,
			.soname       = compiler_arg_empty_1s,
			.rpath        = compiler_arg_empty_1s,
			.sanitize     = compiler_arg_empty_1s,
		}
	};

	struct linker posix = empty;

	struct linker gcc = posix;
	gcc.args.as_needed = linker_gcc_args_as_needed;
	gcc.args.no_undefined = linker_gcc_args_no_undefined;
	gcc.args.start_group = linker_gcc_args_start_group;
	gcc.args.end_group = linker_gcc_args_end_group;
	gcc.args.shared = linker_gcc_args_shared,
	gcc.args.soname = linker_gcc_args_soname,
	gcc.args.rpath = linker_gcc_args_rpath,
	gcc.args.sanitize = compiler_gcc_args_sanitize;

	linkers[linker_posix] = posix;
	linkers[linker_gcc] = gcc;
}

void
compilers_init(void)
{
	build_compilers();
	build_linkers();
}

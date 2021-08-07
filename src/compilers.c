#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "compilers.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/run_cmd.h"

const char *
compiler_type_to_s(enum compiler_type t)
{
	switch (t) {
	case compiler_gcc: return "gcc";
	case compiler_clang: return "clang";
	default: assert(false); return "";
	}
}

static const char *compiler_language_names[compiler_language_count] = {
	[compiler_language_c] = "c",
	[compiler_language_cpp] = "cpp",
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
		[compiler_language_cpp] = { "cc" },
		[compiler_language_cpp_hdr] = { "hpp" },
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
	if (!run_cmd(&cmd_ctx, cc, (char *[]){
		(char *)cc, "--version", NULL,
	})) {
		return false;
	}

	enum compiler_type type;
	if (strstr(cmd_ctx.out, "clang") || strstr(cmd_ctx.out, "Clang")) {
		type = compiler_clang;
	} else if (strstr(cmd_ctx.out, "Free Software Foundation")) {
		type = compiler_gcc;
	} else {
		LOG_E("unknown compiler: '%s'", cmd_ctx.out);
		return false;
	}

	char *p;
	if ((p = strchr(cmd_ctx.out, '\n'))) {
		*p = 0;
	}

	char *ver, *ver_end;
	for (ver = cmd_ctx.out; *ver; ++ver) {
		ver_end = ver;
		while (('0' <= *ver_end && *ver_end <= '9') || *ver_end == '.') {
			++ver_end;
		}

		if (ver != ver_end) {
			*ver_end = 0;
			break;
		}
	}

	LOG_I("detected compiler %s %s (%s)", compiler_type_to_s(type), ver, cc);

	struct obj *comp = make_obj(wk, comp_id, obj_compiler);
	comp->dat.compiler.name = wk_str_push(wk, cc);
	comp->dat.compiler.type = type;
	comp->dat.compiler.version = wk_str_push(wk, ver);
	return true;
}

bool
compiler_detect(struct workspace *wk, uint32_t *comp_id, enum compiler_language lang)
{
	const char *cmd;

	static const char *compiler_env_override[] = {
		[compiler_language_c] = "CC",
		[compiler_language_cpp] = "CXX",
	};

	static const char *compiler_default[] = {
		[compiler_language_c] = "cc",
		[compiler_language_cpp] = "cpp",
	};

	if (!(cmd = getenv(compiler_env_override[lang]))) {
		cmd = compiler_default[lang];
	}

	switch (lang) {
	case compiler_language_c:
	case compiler_language_cpp:
		return compiler_detect_c_or_cpp(wk, cmd, comp_id);
	default:
		assert(false);
		return false;
	}
}

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(*array))
#define COMPILER_ARGS(...) \
	static const char *argv[] = __VA_ARGS__; \
	static struct compiler_args args = { \
		.args = argv, \
		.len = ARRAY_SIZE(argv) \
	};

static const struct compiler_args *
compiler_gcc_args_deps(const char *out_target, const char *out_file)
{
	COMPILER_ARGS({ "-MD", "-MQ", NULL, "-MF", NULL });

	argv[2] = out_target;
	argv[4] = out_file;

	return &args;
}

static const struct compiler_args *
compiler_gcc_args_compile_only(void)
{
	COMPILER_ARGS({ "-c" });

	return &args;
}

static const struct compiler_args *
compiler_gcc_args_output(const char *f)
{
	COMPILER_ARGS({ "-o", NULL });

	argv[1] = f;

	return &args;
}

static const struct compiler_args *
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

static const struct compiler_args *
compiler_gcc_args_debug(void)
{
	COMPILER_ARGS({ "-g" });

	return &args;
}

static const struct compiler_args *
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

static const struct compiler_args *
compiler_gcc_args_set_std(const char *std)
{
	static char buf[BUF_SIZE_S];
	COMPILER_ARGS({ buf });

	snprintf(buf, BUF_SIZE_S, "-std=%s", std);

	return &args;
}

static const struct compiler_args *
compiler_gcc_args_include(const char *dir)
{
	COMPILER_ARGS({ "-I", NULL });

	argv[1] = dir;

	return &args;
}

const struct compiler compilers[] = {
	[compiler_gcc] = {
		.args = {
			.deps = compiler_gcc_args_deps,
			.compile_only = compiler_gcc_args_compile_only,
			.output = compiler_gcc_args_output,
			.optimization = compiler_gcc_args_optimization,
			.debug = compiler_gcc_args_debug,
			.warning_lvl = compiler_gcc_args_warning_lvl,
			.set_std = compiler_gcc_args_set_std,
			.include = compiler_gcc_args_include,
		},
		.deps = compiler_deps_gcc,
	},
	[compiler_clang] = {
		.args = {
			.deps = compiler_gcc_args_deps,
			.compile_only = compiler_gcc_args_compile_only,
			.output = compiler_gcc_args_output,
			.optimization = compiler_gcc_args_optimization,
			.debug = compiler_gcc_args_debug,
			.warning_lvl = compiler_gcc_args_warning_lvl,
			.set_std = compiler_gcc_args_set_std,
			.include = compiler_gcc_args_include,
		},
		.deps = compiler_deps_gcc,
	},
};

const struct language languages[] = {
	[compiler_language_c] = { .is_header = false },
	[compiler_language_c_hdr] = { .is_header = true },
	[compiler_language_cpp] = { .is_header = false },
	[compiler_language_cpp_hdr] = { .is_header = true },
};

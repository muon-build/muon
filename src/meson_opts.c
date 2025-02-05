/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "backend/output.h"
#include "buf_size.h"
#include "error.h"
#include "lang/object_iterators.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"
#include "meson_opts.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "version.h"

struct translate_meson_opts_ctx {
	obj prepend_args;
	obj stray_args;
	obj argv;
	bool help;
	bool introspect_force_object;
	const char *subcommand;
};

struct meson_option_spec {
	const char *name;
	bool has_value;
	const char *corresponding_option;
	bool ignore, help;
	uint32_t handle_as;
};

typedef bool((*translate_meson_opts_func)(struct workspace *wk,
	char *argv[],
	uint32_t argc,
	struct translate_meson_opts_ctx *ctx));
typedef bool((*translate_meson_opts_callback)(struct workspace *wk,
	const struct meson_option_spec *spec,
	const char *val,
	struct translate_meson_opts_ctx *ctx));

static void
translate_meson_opts_help(struct translate_meson_opts_ctx *ctx, const struct meson_option_spec *opts, uint32_t opts_len)
{
	printf("This is the muon meson cli compatibility layer.\n"
	       "usage for subcommand %s:\n",
		ctx->subcommand);

	bool any_ignored = false, indent = true;
	printf("opts:\n");

	uint32_t i;
	for (i = 0; i < opts_len; ++i) {
		any_ignored |= opts[i].ignore;

		if (indent) {
			printf(" ");
		}

		printf("%s%s%s%s%s",
			(opts[i].ignore && log_clr()) ? "\033[31m" : "",
			opts[i].ignore ? "*" : (indent ? " " : ""),
			(opts[i].ignore && log_clr()) ? "\033[0m" : "",
			opts[i].name[1] ? "--" : "-",
			opts[i].name);

		if (i + 1 < opts_len
			&& ((opts[i].help && opts[i + 1].help)
				|| (opts[i].handle_as && opts[i + 1].handle_as
					&& opts[i].handle_as == opts[i + 1].handle_as))) {
			printf(", ");
			indent = false;
			continue;
		}

		if (opts[i].has_value) {
			printf(" <value>");
		} else if (opts[i].help) {
			printf(" - show this message");
		}

		if (opts[i].corresponding_option) {
			printf(" - -D%s=%s", opts[i].corresponding_option, opts[i].has_value ? "<value>" : "true");
		}

		printf("\n");
		indent = true;
	}

	if (any_ignored) {
		printf("%s*%s denotes an ignored option.\n", log_clr() ? "\033[31m" : "", log_clr() ? "\033[0m" : "");
	}
}

static bool
translate_meson_opts_parser(struct workspace *wk,
	char *argv[],
	uint32_t argc,
	struct translate_meson_opts_ctx *ctx,
	const struct meson_option_spec *opts,
	uint32_t opts_len,
	translate_meson_opts_callback cb)
{
	uint32_t argi = 0, i;
	bool is_opt, is_longopt, next_is_val = false;

	const struct meson_option_spec *spec;
	const char *val;

	while (argi < argc) {
		spec = NULL;
		val = NULL;

		struct str *arg = &WKSTR(argv[argi]);
		is_opt = !next_is_val && arg->len > 1 && arg->s[0] == '-';
		is_longopt = is_opt && arg->len > 2 && arg->s[1] == '-';

		if (is_longopt) {
			arg->s += 2;
			arg->len -= 2;

			char *sep;
			if ((sep = strchr(argv[argi], '='))) {
				val = sep + 1;
				arg->len -= (1 + strlen(val));
			}
		} else if (is_opt) {
			arg->s += 1;
			arg->len -= 1;

			if (arg->len > 1) {
				val = arg->s + 1;
				arg->len -= strlen(val);
			}
		}

		if (is_opt) {
			for (i = 0; i < opts_len; ++i) {
				const struct str *opt_name = &WKSTR(opts[i].name);
				if (!str_eql(arg, opt_name)) {
					continue;
				}

				spec = &opts[i];
				++argi;
				break;
			}
		} else {
			obj_array_push(wk, ctx->stray_args, make_str(wk, argv[argi]));
			++argi;
			continue;
		}

		if (!spec) {
			LOG_E("unknown option '%s'", argv[argi]);
			return false;
		}

		if (spec->has_value && !val) {
			if (argi + 1 > argc) {
				LOG_E("option '%s' requires an argument", argv[argi - 1]);
				return false;
			}

			val = argv[argi];
			++argi;
		}

		if (spec->ignore) {
			continue;
		} else if (spec->corresponding_option) {
			if (!spec->has_value) {
				val = "true";
			}

			obj_array_push(wk, ctx->argv, make_strf(wk, "-D%s=%s", spec->corresponding_option, val));
		} else if (spec->help) {
			ctx->help = true;
			translate_meson_opts_help(ctx, opts, opts_len);
		} else if (spec->handle_as) {
			if (!cb(wk, spec, val, ctx)) {
				return false;
			}
		} else {
			UNREACHABLE;
		}
	}

	return true;
}

enum meson_opts_test {
	opt_test_chdir = 1,
	opt_test_no_rebuild,
	opt_test_list,
	opt_test_suite,
	opt_test_benchmark,
	opt_test_workers,
	opt_test_verbose,
	opt_test_setup,
	opt_test_fail_fast,
};

static bool
translate_meson_opts_test_callback(struct workspace *wk,
	const struct meson_option_spec *spec,
	const char *val,
	struct translate_meson_opts_ctx *ctx)
{
	switch ((enum meson_opts_test)(spec->handle_as)) {
	case opt_test_no_rebuild: obj_array_push(wk, ctx->argv, make_str(wk, "-R")); break;
	case opt_test_list: obj_array_push(wk, ctx->argv, make_str(wk, "-l")); break;
	case opt_test_suite: obj_array_push(wk, ctx->argv, make_strf(wk, "-s%s", val)); break;
	case opt_test_benchmark: obj_array_set(wk, ctx->argv, 0, make_str(wk, "benchmark")); break;
	case opt_test_workers: obj_array_push(wk, ctx->argv, make_strf(wk, "-j%s", val)); break;
	case opt_test_verbose: obj_array_push(wk, ctx->argv, make_strf(wk, "-v")); break;
	case opt_test_setup: obj_array_push(wk, ctx->argv, make_strf(wk, "-e%s", val)); break;
	case opt_test_chdir: obj_array_push(wk, ctx->prepend_args, make_strf(wk, "-C%s", val)); break;
	case opt_test_fail_fast: {
		if (strcmp(val, "1") != 0) {
			LOG_E("--maxfail only supports 1, value %s unsupported", val);
			return false;
		}

		obj_array_push(wk, ctx->argv, make_strf(wk, "-f"));
		break;
	}
	default: UNREACHABLE;
	}

	return true;
}

static bool
translate_meson_opts_test(struct workspace *wk, char *argv[], uint32_t argc, struct translate_meson_opts_ctx *ctx)
{
	struct meson_option_spec opts[] = {
		{ "h", .help = true },
		{ "help", .help = true },
		{ "C", true, .handle_as = opt_test_chdir },
		{ "v", .handle_as = opt_test_verbose },
		{ "maxfail", true, .handle_as = opt_test_fail_fast },
		{ "no-rebuild", .handle_as = opt_test_no_rebuild },
		{ "list", .handle_as = opt_test_list },
		{ "suite", true, .handle_as = opt_test_suite },
		{ "benchmark", .handle_as = opt_test_benchmark },
		{ "num-processes", true, .handle_as = opt_test_workers },
		{ "setup", true, .handle_as = opt_test_setup },
		{ "q", .ignore = true, .handle_as = 0xfff0 },
		{ "quiet", .ignore = true, .handle_as = 0xfff0 },
		{ "t", true, .ignore = true, .handle_as = 0xfff1 },
		{ "timeout-multiplier", true, .ignore = true, .handle_as = 0xfff1 },
		{ "gdb", .ignore = true },
		{ "gdb-path", true, .ignore = true },
		{ "repeat", true, .ignore = true },
		{ "wrapper", true, .ignore = true },
		{ "no-suite", .ignore = true },
		{ "no-stdsplit", .ignore = true },
		{ "print-errorlogs", .ignore = true },
		{ "logbase", true, .ignore = true },
		{ "test-args", true, .ignore = true },
	};

	obj_array_push(wk, ctx->argv, make_str(wk, "test"));
	obj_array_push(wk, ctx->argv, make_str(wk, "-v"));

	return translate_meson_opts_parser(
		wk, argv, argc, ctx, opts, ARRAY_LEN(opts), translate_meson_opts_test_callback);
}

enum meson_opts_install {
	opt_install_dryrun = 1,
	opt_install_chdir,
	opt_install_destdir,
};

static bool
translate_meson_opts_install_callback(struct workspace *wk,
	const struct meson_option_spec *spec,
	const char *val,
	struct translate_meson_opts_ctx *ctx)
{
	switch ((enum meson_opts_install)(spec->handle_as)) {
	case opt_install_dryrun: {
		obj_array_push(wk, ctx->argv, make_str(wk, "-n"));
		break;
	}
	case opt_install_chdir: obj_array_push(wk, ctx->prepend_args, make_strf(wk, "-C%s", val)); break;
	case opt_install_destdir: obj_array_push(wk, ctx->argv, make_strf(wk, "-d%s", val)); break;
	default: UNREACHABLE;
	}

	return true;
}

static bool
translate_meson_opts_install(struct workspace *wk, char *argv[], uint32_t argc, struct translate_meson_opts_ctx *ctx)
{
	struct meson_option_spec opts[] = {
		{ "h", .help = true },
		{ "help", .help = true },
		{ "C", true, .handle_as = opt_install_chdir },
		{ "n", .handle_as = opt_install_dryrun },
		{ "dryrun", .handle_as = opt_install_dryrun },
		{ "destdir", true, .handle_as = opt_install_destdir },
		{ "no-rebuild", .ignore = true },
		{ "only-changed", .ignore = true },
		{ "quiet", .ignore = true },
		{ "skip-subprojects", true, .ignore = true },
		{ "tags", true, .ignore = true },
		{ "strip", .ignore = true },
	};

	obj_array_push(wk, ctx->argv, make_str(wk, "install"));

	if (!translate_meson_opts_parser(
		    wk, argv, argc, ctx, opts, ARRAY_LEN(opts), translate_meson_opts_install_callback)) {
		return false;
	}

	return true;
}

enum meson_opts_setup {
	opt_setup_version = 1,
	opt_setup_define,
};

static bool
translate_meson_opts_setup_callback(struct workspace *wk,
	const struct meson_option_spec *spec,
	const char *val,
	struct translate_meson_opts_ctx *ctx)
{
	switch ((enum meson_opts_setup)(spec->handle_as)) {
	case opt_setup_version: {
		obj_array_prepend(wk, &ctx->argv, make_str(wk, "version"));
		break;
	}
	case opt_setup_define: obj_array_push(wk, ctx->argv, make_strf(wk, "-D%s", val)); break;
	default: UNREACHABLE;
	}

	return true;
}

static bool
translate_meson_opts_setup(struct workspace *wk, char *argv[], uint32_t argc, struct translate_meson_opts_ctx *ctx)
{
	struct meson_option_spec opts[] = {
		{ "h", .help = true },
		{ "help", .help = true },
		{ "v", .handle_as = opt_setup_version },
		{ "D", true, .handle_as = opt_setup_define },

		{ "prefix", true, "prefix" },
		{ "bindir", true, "bindir" },
		{ "datadir", true, "datadir" },
		{ "includedir", true, "includedir" },
		{ "infodir", true, "infodir" },
		{ "libdir", true, "libdir" },
		{ "libexecdir", true, "libexecdir" },
		{ "localedir", true, "localedir" },
		{ "localstatedir", true, "localstatedir" },
		{ "mandir", true, "mandir" },
		{ "sbindir", true, "sbindir" },
		{ "sharedstatedir", true, "sharedstatedir" },
		{ "sysconfdir", true, "sysconfdir" },

		{ "auto-features", true, "auto_features" },
		{ "backend", true, "backend" },
		{ "build.cmake-prefix-path", true, "cmake_prefix_path" },
		{ "build.pkg-config-path", true, "pkg_config_path" },
		{ "buildtype", true, "buildtype" },
		{ "cmake-prefix-path", true, "cmake_prefix_path" },
		{ "cross-file", true, .ignore = true },
		{ "debug", false, "debug" },
		{ "default-library", true, "default_library" },
		{ "errorlogs", .ignore = true },
		{ "fatal-meson-warnings", .ignore = true },
		{ "force-fallback-for", true, "force_fallback_for" },
		{ "install-umask", true, "insall_umask" },
		{ "layout", true, "layout" },
		{ "native-file", true, .ignore = true },
		{ "optimization", true, "optimization" },
		{ "pkg-config-path", true, "pkg_config_path" },
		{ "pkgconfig.relocatable", .ignore = true },
		{ "prefer-static", false, "prefer_static" },
		{ "python.install-env", true, .ignore = true },
		{ "python.bytecompile", true, .ignore = true },
		{ "python.platlibdir", true },
		{ "python.purelibdir", true },
		{ "python.allow_limited_api", true, .ignore = true },
		{ "stdsplit", false, "stdsplit" },
		{ "strip", false, "strip" },
		{ "unity", true, "unity" },
		{ "unity-size", true, "unity_size" },
		{ "warnlevel", true, "warning_level" },
		{ "werror", false, "werror" },
		{ "wrap-mode", true, "wrap_mode" },

		{ "reconfigure", .ignore = true },
		{ "version", .handle_as = opt_setup_version },
		{ "vsenv", .ignore = true },
		{ "wipe", .ignore = true },
	};

	obj_array_push(wk, ctx->argv, make_str(wk, "setup"));

	if (!translate_meson_opts_parser(
		    wk, argv, argc, ctx, opts, ARRAY_LEN(opts), translate_meson_opts_setup_callback)) {
		return false;
	}

	if (get_obj_array(wk, ctx->stray_args)->len == 2) {
		// We were passed a source dir and a build dir.  This can be
		// accomplished in muon via -C <source> setup <build>, with the
		// additional requirement that the build dir be specified
		// relative to the source dir, or absolute.
		obj build, src;
		obj_array_index(wk, ctx->stray_args, 0, &build);
		obj_array_index(wk, ctx->stray_args, 1, &src);

		SBUF(build_dir);
		path_make_absolute(wk, &build_dir, get_cstr(wk, build));

		obj_array_push(wk, ctx->prepend_args, make_strf(wk, "-C%s", get_cstr(wk, src)));

		make_obj(wk, &ctx->stray_args, obj_array);
		obj_array_push(wk, ctx->stray_args, sbuf_into_str(wk, &build_dir));
	}

	return true;
}

enum meson_opts_introspect {
	opt_introspect_file = 1,
	opt_introspect_force_object,
};

static bool
translate_meson_opts_introspect_callback(struct workspace *wk,
	const struct meson_option_spec *spec,
	const char *val,
	struct translate_meson_opts_ctx *ctx)
{
	switch ((enum meson_opts_introspect)(spec->handle_as)) {
	case opt_introspect_file:
		obj_array_push(wk, ctx->argv, make_str(wk, spec->name));
		break;
	case opt_introspect_force_object: ctx->introspect_force_object = true; return true;
	default: UNREACHABLE;
	}

	return true;
}

static bool
translate_meson_opts_introspect(struct workspace *wk, char *argv[], uint32_t argc, struct translate_meson_opts_ctx *ctx)
{
	struct meson_option_spec opts[] = {
		{ "h", .help = true },
		{ "help", .help = true },
		{ "a", .ignore = true },
		{ "all", .ignore = true },
		{ "i", .ignore = true },
		{ "indent", .ignore = true },
		{ "backend", true, .ignore = true },
		{ "f", .handle_as = opt_introspect_force_object },
		{ "force-object-output", .handle_as = opt_introspect_force_object },
		{ "ast", .handle_as = opt_introspect_file },
		{ "benchmarks", .handle_as = opt_introspect_file },
		{ "buildoptions", .handle_as = opt_introspect_file },
		{ "buildsystem-files", .handle_as = opt_introspect_file },
		{ "compilers", .handle_as = opt_introspect_file },
		{ "dependencies", .handle_as = opt_introspect_file },
		{ "scan-dependencies", .handle_as = opt_introspect_file },
		{ "installed", .handle_as = opt_introspect_file },
		{ "install-plan", .handle_as = opt_introspect_file },
		{ "machines", .handle_as = opt_introspect_file },
		{ "projectinfo", .handle_as = opt_introspect_file },
		{ "targets", .handle_as = opt_introspect_file },
		{ "tests", .handle_as = opt_introspect_file },
	};

	if (!translate_meson_opts_parser(
		    wk, argv, argc, ctx, opts, ARRAY_LEN(opts), translate_meson_opts_introspect_callback)) {
		return false;
	}

	const char *build_dir = 0;
	if (get_obj_array(wk, ctx->stray_args)->len) {
		obj build;
		obj_array_index(wk, ctx->stray_args, 0, &build);
		build_dir = get_cstr(wk, build);
	}

	uint32_t i = 0, intro_len = get_obj_array(wk, ctx->argv)->len;
	bool make_object = intro_len > 1;
	if (ctx->introspect_force_object) {
		make_object = true;
	}

	SBUF(out);

	if (make_object) {
		sbuf_push(wk, &out, '{');
	}

	obj v;
	obj_array_for(wk, ctx->argv, v) {
			SBUF(path);

			if (make_object) {
				sbuf_pushf(wk, &out, "\"%s\":", get_cstr(wk, v));
			}

			if (build_dir) {
				path_push(wk, &path, build_dir);
			}
			path_push(wk, &path, output_path.introspect_dir);
			path_push(wk, &path, "intro-");
			sbuf_pushf(wk, &path, "%s.json", get_cstr(wk, v));

			struct source src;
			if (!fs_read_entire_file(path.buf, &src)) {
				LOG_E("failed to introspect %s", get_cstr(wk, v));
				return false;
			}

			sbuf_pushn(wk, &out, src.src, src.len);

			if (make_object && i < intro_len - 1) {
				sbuf_push(wk, &out, ',');
			}

			++i;
	}

	if (make_object) {
		sbuf_push(wk, &out, '}');
	}

	printf("%s\n", out.buf);

	exit(0);
}

static const struct {
	const char *name;
	translate_meson_opts_func translate_func;
} meson_opts_subcommands[] = {
	{ "setup", translate_meson_opts_setup },
	{ "configure", translate_meson_opts_setup },
	{ "install", translate_meson_opts_install },
	{ "test", translate_meson_opts_test },
	{ "introspect", translate_meson_opts_introspect },
};

static translate_meson_opts_func
meson_opts_subcommand(const char *arg)
{
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(meson_opts_subcommands); ++i) {
		if (strcmp(arg, meson_opts_subcommands[i].name) == 0) {
			return meson_opts_subcommands[i].translate_func;
		}
	}

	LOG_E("unknown subcommand '%s'", arg);
	return 0;
}

static void
print_meson_opts_usage(void)
{
	printf("opts:\n"
	       "  -v, --version - print the meson compat version and exit\n"
	       "  -h [subcommand] - print this message or show help for a subcommand\n"
	       "commands:\n");

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(meson_opts_subcommands); ++i) {
		printf("  %s\n", meson_opts_subcommands[i].name);
	}
}

bool
translate_meson_opts(struct workspace *wk,
	uint32_t argc,
	uint32_t argi,
	char *argv[],
	uint32_t *new_argc,
	uint32_t *new_argi,
	char **new_argv[])
{
	if (argc - argi < 1) {
		print_meson_opts_usage();
		return false;
	}

	translate_meson_opts_func translate_func = 0;

	if (strcmp(argv[argi], "-v") == 0 || strcmp(argv[argi], "--version") == 0) {
		printf("%s\n", muon_version.meson_compat);
		exit(0);
	} else if (strcmp(argv[argi], "-h") == 0) {
		printf("This is the muon meson cli compatibility layer.\n");
		print_meson_opts_usage();
		exit(0);
	}

	if (!(translate_func = meson_opts_subcommand(argv[argi]))) {
		return false;
	}

	struct translate_meson_opts_ctx ctx = { .subcommand = argv[argi] };
	make_obj(wk, &ctx.argv, obj_array);
	make_obj(wk, &ctx.prepend_args, obj_array);
	make_obj(wk, &ctx.stray_args, obj_array);

	++argi;
	if (!translate_func(wk, argv + argi, argc - argi, &ctx)) {
		/* LOG_E("failed to translate"); */
		return false;
	}

	if (ctx.help) {
		exit(0);
	}

	obj_array_prepend(wk, &ctx.prepend_args, make_str(wk, argv[0]));
	obj_array_extend_nodup(wk, ctx.prepend_args, ctx.argv);
	ctx.argv = ctx.prepend_args;
	obj_array_extend_nodup(wk, ctx.argv, ctx.stray_args);

	obj_lprintf(wk, "args: %o\n", ctx.argv);

	const char *argstr;
	join_args_argstr(wk, &argstr, new_argc, ctx.argv);
	argstr_to_argv(argstr, *new_argc, NULL, (char *const **)new_argv);
	*new_argi = 0;
	return true;
}

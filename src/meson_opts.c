/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "buf_size.h"
#include "datastructures/arr.h"
#include "error.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"
#include "meson_opts.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "version.h"

#include <string.h>
#include <stdlib.h>

struct translate_meson_opts_ctx {
	obj prepend_args;
	obj stray_args;
	obj argv;
};

struct meson_option_spec {
	const char *name;
	bool has_value;
	const char *corresponding_option;
	bool ignore, help;
	uint32_t handle_as;
};

typedef bool ((*translate_meson_opts_func)(struct workspace *wk, char *argv[],
	uint32_t argc, struct translate_meson_opts_ctx *ctx));
typedef bool ((*translate_meson_opts_callback)(struct workspace *wk,
	const struct meson_option_spec *spec, const char *val, struct translate_meson_opts_ctx *ctx));

static bool
translate_meson_opts_parser(struct workspace *wk, char *argv[], uint32_t argc, struct translate_meson_opts_ctx *ctx,
	const struct meson_option_spec *opts, uint32_t opts_len, translate_meson_opts_callback cb)
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
				arg->len -= 1 - strlen(val);
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
				LOG_E("option '%s' requires an argument", argv[argi]);
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
			obj_array_push(wk, ctx->argv, make_str(wk, "-h"));
		} else if (spec->handle_as) {
			cb(wk, spec, val, ctx);
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
};

static bool
translate_meson_opts_test_callback(struct workspace *wk,
	const struct meson_option_spec *spec, const char *val, struct translate_meson_opts_ctx *ctx)
{
	switch ((enum meson_opts_test)(spec->handle_as)) {
	case opt_test_no_rebuild:
		obj_array_push(wk, ctx->argv, make_str(wk, "-R"));
		break;
	case opt_test_list:
		obj_array_push(wk, ctx->argv, make_str(wk, "-l"));
		break;
	case opt_test_suite:
		obj_array_push(wk, ctx->argv, make_strf(wk, "-s%s", val));
		break;
	case opt_test_benchmark:
		obj_array_set(wk, ctx->argv, 0, make_str(wk, "benchmark"));
		break;
	case opt_test_workers:
		obj_array_push(wk, ctx->argv, make_strf(wk, "-j%s", val));
		break;
	case opt_test_verbose:
		obj_array_push(wk, ctx->argv, make_strf(wk, "-vv"));
		break;
	case opt_test_setup:
		obj_array_push(wk, ctx->argv, make_strf(wk, "-e%s", val));
		break;
	case opt_test_chdir:
		obj_array_push(wk, ctx->prepend_args, make_strf(wk, "-C%s", val));
		break;
	default:
		UNREACHABLE;
	}

	return true;
}

static bool
translate_meson_opts_test(struct workspace *wk, char *argv[], uint32_t argc, struct translate_meson_opts_ctx *ctx)
{
	struct meson_option_spec opts[] = {
		{ "h", .help = true },
		{ "help", .help = true },
		{ "maxfail", true, .ignore = true },
		{ "repeat", true, .ignore = true },
		{ "no-rebuild", .handle_as = opt_test_no_rebuild },
		{ "gdb", .ignore = true },
		{ "gdb-path", true, .ignore = true },
		{ "list", .handle_as = opt_test_list },
		{ "wrapper", true, .ignore = true, },
		{ "C", true, .handle_as = opt_test_chdir },
		{ "suite", true, .handle_as = opt_test_suite },
		{ "no-suite", .ignore = true },
		{ "no-stdsplit", .ignore = true },
		{ "print-errorlogs", .ignore = true },
		{ "benchmark", .handle_as = opt_test_benchmark },
		{ "logbase", true, .ignore = true },
		{ "num-processes", true, .handle_as = opt_test_workers },
		{ "v", .handle_as = opt_test_verbose },
		{ "q", .ignore = true },
		{ "t", true, .ignore = true },
		{ "timeout-multiplier", true, .ignore = true },
		{ "setup", true, .handle_as = opt_test_setup },
		{ "test-args", true, .ignore = true },
	};

	obj_array_push(wk, ctx->argv, make_str(wk, "test"));
	obj_array_push(wk, ctx->argv, make_str(wk, "-v"));

	return translate_meson_opts_parser(wk, argv, argc, ctx, opts, ARRAY_LEN(opts), translate_meson_opts_test_callback);
}

enum meson_opts_install {
	opt_install_dryrun = 1,
	opt_install_chdir,
};

static bool
translate_meson_opts_install_callback(struct workspace *wk,
	const struct meson_option_spec *spec, const char *val, struct translate_meson_opts_ctx *ctx)
{
	switch ((enum meson_opts_install)(spec->handle_as)) {
	case opt_install_dryrun: {
		obj_array_push(wk, ctx->argv, make_str(wk, "-n"));
		break;
	}
	case opt_install_chdir:
		obj_array_push(wk, ctx->prepend_args, make_strf(wk, "-C%s", val));
		break;
	default:
		UNREACHABLE;
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
		{ "no-rebuild", .ignore = true },
		{ "only-changed", .ignore = true },
		{ "quiet", .ignore = true },
		{ "dryrun", .handle_as = opt_install_dryrun },
		{ "n", .handle_as = opt_install_dryrun },
		{ "destdir", .ignore = true },
		{ "skip-subprojects", true, .ignore = true },
		{ "tags", true, .ignore = true },
		{ "strip", .ignore = true },
	};

	obj_array_push(wk, ctx->argv, make_str(wk, "install"));

	return translate_meson_opts_parser(wk, argv, argc, ctx, opts, ARRAY_LEN(opts), translate_meson_opts_install_callback);
}

enum meson_opts_setup {
	opt_setup_version = 1,
	opt_setup_define,
};

static bool
translate_meson_opts_setup_callback(struct workspace *wk,
	const struct meson_option_spec *spec, const char *val, struct translate_meson_opts_ctx *ctx)
{
	switch ((enum meson_opts_setup)(spec->handle_as)) {
	case opt_setup_version: {
		obj_array_prepend(wk, &ctx->argv, make_str(wk, "version"));
		break;
	}
	case opt_setup_define:
		obj_array_push(wk, ctx->argv, make_strf(wk, "-D%s", val));
		break;
	default:
		UNREACHABLE;
	}

	return true;
}

static bool
translate_meson_opts_setup(struct workspace *wk, char *argv[], uint32_t argc, struct translate_meson_opts_ctx *ctx)
{
	obj_array_push(wk, ctx->argv, make_str(wk, "setup"));

	struct meson_option_spec opts[] = {
		{ "bindir", true, "bindir", },
		{ "datadir", true, "datadir" },
		{ "includedir", true, "includedir" },
		{ "infodir", true, "infodir" },
		{ "libdir", true, "libdir", },
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
		{ "cross-file", true, .ignore = true, },
		{ "debug", false, "debug", },
		{ "default-library", true, "default_library" },
		{ "errorlogs", .ignore = true },
		{ "fatal-meson-warnings", .ignore = true },
		{ "force-fallback-for", true, "force_fallback_for", },
		{ "install-umask", true, "insall_umask" },
		{ "layout", true, "layout", },
		{ "native-file", true, .ignore = true },
		{ "optimization", true, "optimization" },
		{ "pkg-config-path", true, "pkg_config_path", },
		{ "pkgconfig.relocatable", .ignore = true },
		{ "prefer-static", false, "prefer_static" },
		{ "prefix", true, "prefix" },
		{ "python.install-env", true, .ignore = true },
		{ "python.platlibdir", true, .ignore = true },
		{ "python.purelibdir", true, .ignore = true },
		{ "stdsplit", false, "stdsplit" },
		{ "strip", false, "strip" },
		{ "unity", true, "unity" },
		{ "unity-size", true, "unity_size" },
		{ "warnlevel", true, "warning_level" },
		{ "werror", false, "werror" },
		{ "wrap-mode", true, "wrap_mode" },

		{ "help", .help = true },
		{ "reconfigure", .ignore = true },
		{ "version", .handle_as = opt_setup_version },
		{ "vsenv", .ignore = true },
		{ "wipe", .ignore = true },

		{ "D", true, .handle_as = opt_setup_define },
		{ "h", .help = true },
		{ "v", .handle_as = opt_setup_version },
	};

	if (!translate_meson_opts_parser(wk, argv, argc, ctx, opts, ARRAY_LEN(opts), translate_meson_opts_setup_callback)) {
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

bool
translate_meson_opts(struct workspace *wk, uint32_t argc, uint32_t argi, char *argv[],
	uint32_t *new_argc, uint32_t *new_argi, char **new_argv[])
{
	if (argc - argi < 1) {
		LOG_E("missing subcommand");
		return false;
	}

	translate_meson_opts_func translate_func;

	if (strcmp(argv[argi], "setup") == 0 || strcmp(argv[argi], "configure") == 0) {
		translate_func = translate_meson_opts_setup;
	} else if (strcmp(argv[argi], "install") == 0) {
		translate_func = translate_meson_opts_install;
	} else if (strcmp(argv[argi], "test") == 0) {
		translate_func = translate_meson_opts_test;
	} else if (strcmp(argv[argi], "-v") == 0 || strcmp(argv[argi], "--version") == 0) {
		printf("%s\n", muon_version.meson_compat);
		exit(0);
	} else if (strcmp(argv[argi], "-h") == 0) {
		printf("This is the muon meson cli compatibility layer.  Help not available.\n");
		exit(1);
	} else {
		LOG_E("unknown subcommand '%s'", argv[argi]);
		return false;
	}

	struct translate_meson_opts_ctx ctx = { 0 };
	make_obj(wk, &ctx.argv, obj_array);
	make_obj(wk, &ctx.prepend_args, obj_array);
	make_obj(wk, &ctx.stray_args, obj_array);

	++argi;
	if (!translate_func(wk, argv + argi, argc - argi, &ctx)) {
		/* LOG_E("failed to translate"); */
		return false;
	}

	obj_array_prepend(wk, &ctx.prepend_args, make_str(wk, argv[0]));
	obj_array_extend_nodup(wk, ctx.prepend_args, ctx.argv);
	ctx.argv = ctx.prepend_args;
	obj_array_extend_nodup(wk, ctx.argv, ctx.stray_args);

	obj_fprintf(wk, log_file(), "args: %o\n", ctx.argv);

	const char *argstr;
	join_args_argstr(wk, &argstr, new_argc, ctx.argv);
	argstr_to_argv(argstr, *new_argc, NULL, (char *const **)new_argv);
	*new_argi = 0;
	return true;
}

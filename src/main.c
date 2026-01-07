/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net>
 * SPDX-FileCopyrightText: Simon Zeni <simon@bl4ckb0ne.ca>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "backend/backend.h"
#include "backend/common_args.h"
#include "backend/ninja.h"
#include "backend/output.h"
#include "buf_size.h"
#include "cmd_install.h"
#include "cmd_subprojects.h"
#include "cmd_test.h"
#include "embedded.h"
#include "error.h"
#include "external/libarchive.h"
#include "external/libcurl.h"
#include "external/pkgconfig.h"
#include "external/samurai.h"
#include "lang/analyze.h"
#include "lang/docs.h"
#include "lang/fmt.h"
#include "lang/lsp.h"
#include "lang/object_iterators.h"
#include "lang/parser.h"
#include "lang/serial.h"
#include "meson_opts.h"
#include "options.h"
#include "opts.h"
#include "platform/assert.h"
#include "platform/backtrace.h"
#include "platform/init.h"
#include "platform/os.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "tracy.h"
#include "ui.h"
#include "version.h"
#include "vsenv.h"

static bool
ensure_in_build_dir(void)
{
	if (!fs_dir_exists(output_path.private_dir)) {
		LOG_E("this subcommand must be run from a build directory");
		return false;
	}

	return true;
}

static bool
load_obj_from_serial_dump(struct workspace *wk, const char *path, obj *res)
{
	bool ret = false;
	FILE *f;
	if (!(f = fs_fopen(path, "rb"))) {
		return false;
	}

	if (!serial_load(wk, res, f)) {
		LOG_E("failed to load environment data");
		goto ret;
	}

	ret = true;
ret:
	if (!fs_fclose(f)) {
		ret = false;
	}
	return ret;
}

static bool
cmd_exe(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *feed;
		const char *capture;
		const char *environment;
		const char *args;
		const char *const *cmd;
		const char *remove_before_running;
	} opts = { 0 };

	opt_for(-1, .usage_post = " cmd [args]") {
		if (opt_match('f', "feed file to input", "file")) {
			opts.feed = opt_ctx.optarg;
		} else if (opt_match('c', "capture output to file", "file")) {
			opts.capture = opt_ctx.optarg;
		} else if (opt_match('e', "load environment from data file", "file")) {
			opts.environment = opt_ctx.optarg;
		} else if (opt_match('a', "load arguments from data file", "file")) {
			opts.args = opt_ctx.optarg;
		} else if (opt_match('R', "remove file if it exists before executing the command", "file")) {
			opts.remove_before_running = opt_ctx.optarg;
		}
	}
	opt_end();

	if (argi >= argc && !opts.args) {
		LOG_E("missing command");
		return false;
	} else if (argi < argc && opts.args) {
		LOG_E("command cannot be specified by trailing arguments *and* -a");
		return false;
	}

	opts.cmd = (const char *const *)&argv[argi];
	++argi;

	if (opts.remove_before_running && fs_exists(opts.remove_before_running)) {
		if (!fs_remove(opts.remove_before_running)) {
			return false;
		}
	}

	bool ret = false;
	struct run_cmd_ctx ctx = { 0 };
	ctx.stdin_path = opts.feed;

	if (!opts.capture) {
		ctx.flags |= run_cmd_ctx_flag_dont_capture;
	}

	const char *envstr = NULL;
	uint32_t envc = 0;
	if (opts.environment) {
		obj env;
		if (!load_obj_from_serial_dump(wk, opts.environment, &env)) {
			goto ret;
		}

		env_to_envstr(wk, &envstr, &envc, env);
	}

	if (opts.args) {
		obj args;
		if (!load_obj_from_serial_dump(wk, opts.args, &args)) {
			goto ret;
		}

		const char *argstr;
		uint32_t argc;
		join_args_argstr(wk, &argstr, &argc, args);
		argstr_to_argv(wk, argstr, argc, NULL, (char *const **)&opts.cmd);
	}

	if (!run_cmd_argv(wk, &ctx, (char *const *)opts.cmd, envstr, envc)) {
		LOG_E("failed to run command: %s", ctx.err_msg);
		goto ret;
	}

	if (ctx.status != 0) {
		if (opts.capture) {
			fputs(ctx.err.buf, stderr);
		}
		goto ret;
	}

	if (opts.capture) {
		ret = fs_write(opts.capture, (uint8_t *)ctx.out.buf, ctx.out.len);
	} else {
		ret = true;
	}
ret:
	run_cmd_ctx_destroy(&ctx);
	return ret;
}

static const struct opt_match_enum_table opt_language_mode_table[] = {
	{ "normal", language_external, "n" },
	{ "script", language_internal, "s" },
	{ "module", language_extended, "m" },
};

static bool
language_mode_from_optarg(const char *arg, enum language_mode *langmode)
{
	struct {
		const char *short_name, *long_name;
		enum language_mode mode;
	} modes[] = {
		{ "n", "normal", language_external },
		{ "s", "script", language_internal },
		{ "m", "module", language_extended },
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(modes); ++i) {
		if (strcmp(arg, modes[i].short_name) == 0 || strcmp(arg, modes[i].long_name) == 0) {
			*langmode = modes[i].mode;
			return true;
		}
	}

	LOG_E("invalid language mode: %s", arg);
	LOG_I("supported language modes are:");
	for (i = 0; i < ARRAY_LEN(modes); ++i) {
		LOG_I("  - %s | %s", modes[i].short_name, modes[i].long_name);
	}
	return false;
}

static bool
cmd_check(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *filename;
		const char *breakpoint;
		bool print_ast, print_dis;
		enum vm_compile_mode compile_mode;
	} opts = { 0 };

	opt_for(1, .usage_post = " <filename>") {
		if (opt_match('p', "print parsed ast")) {
			opts.print_ast = true;
		} else if (opt_match('d', "print dissasembly")) {
			opts.print_dis = true;
		} else if (opt_match('b', "set breakpoint", "breakpoint")) {
			opts.breakpoint = opt_ctx.optarg;
		} else if (opt_match('m', "parse with language mode", opt_match_enum_table(opt_language_mode_table))) {
			{
				enum language_mode mode;
				if (!language_mode_from_optarg(opt_ctx.optarg, &mode)) {
					return false;
				}

				if (mode == language_internal || mode == language_extended) {
					opts.compile_mode |= vm_compile_mode_language_extended;
				}
			}
		} else if (opt_match('f', "enable formatting mode")) {
			opts.compile_mode |= vm_compile_mode_fmt;
		} else if (opt_match('r', "enable relaxed mode")) {
			opts.compile_mode |= vm_compile_mode_relaxed_parse;
		}
	}
	opt_end();

	opts.filename = argv[argi];

	arr_push(wk->a, &wk->vm.src, &(struct source){ 0 });
	struct source *src = arr_get(&wk->vm.src, 0);

	if (!fs_read_entire_file(wk->a_scratch, opts.filename, src)) {
		return false;
	}

	if (opts.breakpoint) {
		if (!vm_dbg_push_breakpoint_str(wk, opts.breakpoint)) {
			return false;
		}
	}

	if (opts.print_ast) {
		struct node *n;
		if (!(n = parse(wk, src, opts.compile_mode))) {
			return false;
		}

		if (opts.compile_mode & vm_compile_mode_fmt) {
			print_fmt_ast(wk, n);
		} else {
			print_ast(wk, n);
		}
	} else {
		uint32_t _entry;
		if (!vm_compile(wk, src, opts.compile_mode, &_entry)) {
			return false;
		}

		if (opts.print_dis) {
			vm_dis(wk);
		}
	}

	return true;
}

static bool
cmd_analyze(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		bool subdir_error;
		enum error_diagnostic_store_replay_opts replay_opts;
		const char *file_override;
		uint64_t enabled_diagnostics;
		enum {
			action_file,
			action_lsp,
			action_root_for,
			action_trace,
			action_default,
			action_count,
		} action;
		enum language_mode lang_mode;
	} opts = {
		.enabled_diagnostics = az_diagnostic_unused_variable | az_diagnostic_dead_code,
		.action = action_default,
		.lang_mode = language_external,
	};

	static struct opt_command commands[] = {
		{ "file",
			0,
			"analyze a single file.  Implies -m module.",
			"-m script can also be passed to analyze a file in script mode. This "
			"is useful for catching bugs in scripts that will be evaluated with "
			"*muon internal eval*, or script modules." },
		{ "lsp", 0, "run the analyzer as a language server" },
		{ "root-for", 0, "determine the project root given a meson file" },
		{ "trace", 0, "print a tree of all meson source files that are evaluated" },
		0,
	};
	static const int32_t command_args[action_count] = {
		[action_root_for] = 1,
		[action_file] = 1,
	};

	opt_for(-1, commands) {
		if (opt_match('m', "analyze with language mode", opt_match_enum_table(opt_language_mode_table))) {
			opts.lang_mode = opt_ctx.optarg_enum_value;
		} else if (opt_match('l', "optimize output for editor linter plugins")) {
			opts.subdir_error = true;
			opts.replay_opts |= error_diagnostic_store_replay_dont_include_sources;
		} else if (opt_match('O', "read project file with matching path from stdin", "path")) {
			opts.file_override = opt_ctx.optarg;
		} else if (opt_match('q', "only report errors")) {
			opts.replay_opts |= error_diagnostic_store_replay_errors_only;
		} else if (
			opt_match('W',
				"enable or disable diagnostics",
				"diagnostic",
				"-Wlist can be used to list all supported diagnostics. -Werror will turn all warnings into errors.")) {
			bool enable = true;
			const char *name = opt_ctx.optarg;
			if (str_startswith(&STRL(opt_ctx.optarg), &STR("no-"))) {
				enable = false;
				name += 3;
			}

			if (strcmp(name, "list") == 0) {
				az_print_diagnostic_names();
				return true;
			} else if (strcmp(name, "error") == 0) {
				opts.replay_opts |= error_diagnostic_store_replay_werror;
			} else {
				enum az_diagnostic d;
				if (!az_diagnostic_name_to_enum(name, &d)) {
					LOG_E("invalid diagnostic name '%s'", name);
					return false;
				}

				if (enable) {
					opts.enabled_diagnostics |= d;
				} else {
					opts.enabled_diagnostics &= ~d;
				}
			}
		}
	}
	opt_end();

	{
		// Determine "action".  This is basically a subcommand but is allowed
		// to be empty
		uint32_t cmd_i = action_default;
		if (!opt_find_cmd(commands, &cmd_i, argc, argi, argv, true)) {
			return false;
		}
		if (cmd_i != action_default) {
			++argi;
		}
		opts.action = cmd_i;

		if (!opt_check_operands(argc, argi, command_args[opts.action])) {
			return false;
		}
	}

	if (opts.action == action_lsp) {
		struct az_opts az_opts = {
			.enabled_diagnostics = opts.enabled_diagnostics,
		};
		return analyze_server(wk, &az_opts);
	} else if (opts.action == action_root_for) {
		const char *root = determine_project_root(wk, argv[argi]);
		if (root) {
			printf("%s\n", root);
		}

		return root ? true : false;
	} else {
		const char *single_file = 0;
		if (opts.action == action_file) {
			single_file = argv[argi];
			if (opts.lang_mode == language_external) {
				opts.lang_mode = language_extended;
			}
		}

		struct az_opts az_opts;
		analyze_opts_init(wk, &az_opts);
		az_opts.eval_trace = opts.action == action_trace;
		az_opts.subdir_error = opts.subdir_error;
		az_opts.replay_opts = opts.replay_opts;
		az_opts.single_file = single_file;
		az_opts.enabled_diagnostics = opts.enabled_diagnostics;
		az_opts.auto_chdir_root = true;
		az_opts.lang_mode = opts.lang_mode;

		bool res = true;
		if (opts.file_override) {
			res = analyze_opts_push_override(wk, &az_opts, opts.file_override, "-", 0);
		}

		if (res) {
			res = do_analyze(wk, &az_opts);
		}

		return res;
	}
}

static bool
cmd_options(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct list_options_opts opts = { 0 };
	opt_for() {
		if (opt_match('a', "list all options")) {
			opts.list_all = true;
		} else if (opt_match('m', "list only modified options")) {
			opts.only_modified = true;
		}
	}
	opt_end();

	return list_options(wk, &opts);
}

static bool
cmd_summary(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	opt_for() {
	}
	opt_end();

	if (!ensure_in_build_dir()) {
		return false;
	}

	TSTR(path);
	path_join(wk, &path, output_path.private_dir, output_path.paths[output_path_summary].path);

	struct source src = { 0 };
	if (!fs_read_entire_file(wk->a_scratch, path.buf, &src)) {
		return false;
	}

	fwrite(src.src, 1, src.len, stdout);

	return true;
}

static bool
cmd_eval(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	const char *string_src = 0;
	bool embedded = false;

	opt_for(-1, .usage_post = " <filename> [args]") {
		if (opt_match('e', "lookup <filename> as an embedded script")) {
			embedded = true;
		} else if (opt_match('s', "disable functions for fuzzing")) {
			wk->vm.disable_fuzz_unsafe_functions = true;
		} else if (opt_match('b', "set breakpoint", "breakpoint")) {
			vm_dbg_push_breakpoint_str(wk, opt_ctx.optarg);
		} else if (opt_match('c', "evaluate program passed in as string", "program text")) {
			string_src = opt_ctx.optarg;
		}
	}
	opt_end();

	bool ret = false;

	struct source src = { 0 };

	wk->vm.lang_mode = language_internal;

	workspace_setup_paths(wk, path_cwd(), argv[0], argc, argv);

	if (string_src) {
		if (!opt_check_operands(argc, argi, 0)) {
			return false;
		}

		src.label = "commandline";
		src.src = string_src;
		src.len = strlen(string_src);
	} else {
		if (argi >= argc) {
			LOG_E("missing required filename argument");
			return false;
		}

		const char *filename = 0;
		filename = argv[argi];
		if (embedded) {
			if (!(embedded_get(wk, filename, &src))) {
				LOG_E("failed to find '%s' in embedded sources", filename);
				goto ret;
			}
		} else {
			if (!fs_read_entire_file(wk->a_scratch, filename, &src)) {
				goto ret;
			}
		}
	}

	{ // populate argv array
		obj argv_obj;
		argv_obj = make_obj(wk, obj_array);
		wk->vm.behavior.assign_variable(wk, "argv", argv_obj, 0, assign_local);

		uint32_t i;
		for (i = argi; i < argc; ++i) {
			obj_array_push(wk, argv_obj, make_str(wk, argv[i]));
		}
	}

	obj res;
	if (!eval(wk, &src, build_language_meson, 0, &res)) {
		goto ret;
	}

	ret = true;
ret:
	return ret;
}

static bool
cmd_repl(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	opt_for() {
	}
	opt_end();

	wk->vm.lang_mode = language_internal;

	workspace_init_runtime(wk);
	workspace_init_startup_files(wk);
	make_dummy_project(wk, false);

	repl(wk, false);
	return true;
}

static bool cmd_main(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[]);

static bool
cmd_dump_docs(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		enum dump_function_docs_output output;
		bool cli;
	} opts = {
		.output = dump_function_docs_output_html,
	};

	static const struct opt_match_enum_table output_table[] = {
		{ "man", dump_function_docs_output_man },
		{ "html", dump_function_docs_output_html },
		{ "json", dump_function_docs_output_json },
	};

	opt_for() {
		if (opt_match('o', "set output type", opt_match_enum_table(output_table))) {
			opts.output = opt_ctx.optarg_enum_value;
		} else if (opt_match('c', "dump cli docs")) {
			opts.cli = true;
		}
	}
	opt_end();

	log_set_file(wk, stderr);

	struct dump_function_docs_opts dump_opts = {
		.type = opts.output,
		.out = stdout,
	};

	if (opts.cli) {
		opt_gather_all(wk, cmd_main);
		const struct opt_gather_all_ctx *ga = &opt_gather_all_ctx;
		dump_cli_docs(wk, &dump_opts, &ga->commands);
	} else {
		workspace_init_runtime(wk);
		workspace_init_startup_files(wk);
		make_dummy_project(wk, true);

		dump_function_docs(wk, &dump_opts);
	}

	return true;
}

static bool
cmd_dump_toolchains(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	bool set_linker = false, set_archiver = false;

	const char *n1_args[32] = { "<value1>", "<value2>" };
	struct args n1 = { n1_args, 2 };
	struct toolchain_dump_opts opts = {
		.s1 = "<value1>",
		.s2 = "<value2>",
		.b1 = true,
		.i1 = 0,
		.n1 = &n1,
	};

	obj comp = 0;
	struct obj_compiler *compiler = 0;

	const struct opt_gather_all_ctx *ga = &opt_gather_all_ctx;
	if (!ga->enabled) {
		workspace_init_runtime(wk);
		workspace_init_startup_files(wk);

		comp = make_obj(wk, obj_compiler);
		compiler = get_obj_compiler(wk, comp);
	}

	opt_for() {
		if (opt_match('t', "set the type for a component or list all component types", "component>=<type>|list" )) {
			if (strcmp(opt_ctx.optarg, "list") == 0) {
				printf("registered toolchains:\n");
				enum toolchain_component component;
				for (component = 0; component < toolchain_component_count; ++component) {
					uint32_t i;
					printf("  %s\n", toolchain_component_to_s(component));
					for (i = 0; i < wk->toolchain_registry.components[component].len; ++i) {
						struct toolchain_id *id
							= arr_get(&wk->toolchain_registry.components[component], i);
						printf("    %s\n", id->id);
					}
				}
				return true;
			}

			char *sep;
			if (!(sep = strchr(opt_ctx.optarg, '='))) {
				LOG_E("invalid type: %s", opt_ctx.optarg);
				return false;
			}

			*sep = 0;

			uint32_t component;
			const char *type = sep + 1;

			if (!toolchain_component_from_s(wk, opt_ctx.optarg, &component)) {
				LOG_E("unknown toolchain component: %s", opt_ctx.optarg);
				return false;
			}

			if (!toolchain_component_type_from_s(wk, component, type, &compiler->type[component])) {
				LOG_E("unknown %s type: %s", toolchain_component_to_s(component), type);
				return false;
			}

			switch ((enum toolchain_component)component) {
			case toolchain_component_compiler: {
				struct toolchain_registry_component *c = arr_get(
					&wk->toolchain_registry.components[component], compiler->type[component]);

				if (!set_linker) {
					compiler->type[toolchain_component_linker]
						= c->sub_components[toolchain_component_linker].type;
				}

				if (!set_archiver) {
					compiler->type[toolchain_component_archiver]
						= c->sub_components[toolchain_component_archiver].type;
				}
				break;
			}
			case toolchain_component_linker: set_linker = true; break;
			case toolchain_component_archiver: set_archiver = true; break;
			}
		} else if (opt_match('s', "set the value for a template argument", "key>=<val")) {
			char *sep;
			if (!(sep = strchr(opt_ctx.optarg, '='))) {
				LOG_E("invalid argument setting: %s", opt_ctx.optarg);
				return false;
			}

			*sep = 0;
			++sep;

			if (strcmp(opt_ctx.optarg, "s1") == 0) {
				opts.s1 = sep;
			} else if (strcmp(opt_ctx.optarg, "s2") == 0) {
				opts.s2 = sep;
			} else if (strcmp(opt_ctx.optarg, "b1") == 0) {
				if (strcmp(sep, "true") == 0) {
					opts.b1 = true;
				} else if (strcmp(sep, "false") == 0) {
					opts.b1 = false;
				} else {
					LOG_E("invalid value for bool: %s", sep);
					return false;
				}
			} else if (strcmp(opt_ctx.optarg, "i1") == 0) {
				int64_t res;
				if (!str_to_i(&STRL(sep), &res, false)) {
					LOG_E("invalid value for integer: %s", sep);
					return false;
				}

				opts.i1 = res;
			} else if (strcmp(opt_ctx.optarg, "n1") == 0) {
				n1.len = 0;

				while (*sep) {
					if (n1.len >= ARRAY_LEN(n1_args)) {
						LOG_E("too many arguments for n1 value");
						return false;
					}

					n1.args[n1.len] = sep;
					++n1.len;

					sep = strchr(sep, ',');
					if (!sep) {
						break;
					}
					*sep = 0;
					++sep;
				}
			} else {
				LOG_E("invalid setting name: %s", opt_ctx.optarg);
				return false;
			}
		}
	}
	opt_end();

	make_dummy_project(wk, true);

	printf("compiler: %s, linker: %s, archiver: %s\n",
		toolchain_component_type_to_id(wk, toolchain_component_compiler, compiler->type[toolchain_component_compiler])->id,
		toolchain_component_type_to_id(wk, toolchain_component_linker, compiler->type[toolchain_component_linker])->id,
		toolchain_component_type_to_id(wk, toolchain_component_archiver, compiler->type[toolchain_component_archiver])->id);
	printf("template arguments: s1: \"%s\", s2: \"%s\", b1: %s, i1: %d, n1: {",
		opts.s1,
		opts.s2,
		opts.b1 ? "true" : "false",
		opts.i1);
	for (uint32_t i = 0; i < opts.n1->len; ++i) {
		printf("\"%s\"", opts.n1->args[i]);
		if (i + 1 < opts.n1->len) {
			printf(", ");
		}
	}
	printf("}\n");

	toolchain_dump(wk, comp, &opts);

	return true;
}

static bool
cmd_internal(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	static struct opt_command commands[] = {
		{ "check", cmd_check, "parse and compile meson files" },
		{ "dump_toolchains", cmd_dump_toolchains, "output toolchain arguments" },
		{ "dump_docs", cmd_dump_docs, "output docs" },
		{ "eval",
			cmd_eval,
			"evaluate a file",
			"The execution environment is restricted, function availability is marked in the reference manual." },
		{ "exe", cmd_exe, "run an external command" },
		{ "repl", cmd_repl, "start a meson language repl" },
		{ "summary", cmd_summary, "print a configured project's summary" },
		0,
	};

	opt_for(-1, commands) {
	}
	opt_end();

	uint32_t cmd_i;
	if (!opt_find_cmd(commands, &cmd_i, argc, argi, argv, false)) {
		return false;
	}

	return commands[cmd_i].cmd(wk, argc, argi, argv);
}

static bool
cmd_samu(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	setup_platform_env(wk, ".", setup_platform_env_requirement_from_cache);
	bool res = samu_main(wk, argc - argi, (char **)&argv[argi], 0);
	return res;
}

static bool
cmd_test(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct opt_match_enum_table progress_display_table[] = {
		{ "auto", test_display_auto },
		{ "dots", test_display_dots },
		{ "bar", test_display_bar },
	};

	static const struct opt_match_enum_table output_table[] = {
		{ "term", test_output_term },
		{ "html", test_output_html },
		{ "json", test_output_json },
	};

	struct test_options test_opts = {
		.timeout_multiplier = 1.0f,
	};

	opt_for(-1, .usage_post = " [test [test [...]]]") {
		if (opt_match('a', "include all tests from all projects")) {
			test_opts.include_subprojects = true;
		} else if (opt_match('b', "run benchmarks instead of tests")) {
			test_opts.cat = test_category_benchmark;
			test_opts.print_summary = true;
		} else if (opt_match('l', "list tests that would be run")) {
			test_opts.list = true;
		} else if (opt_match('e', "use test setup", "setup")) {
			test_opts.setup = opt_ctx.optarg;
		} else if (opt_match('s', "only run items in <suite>, may be passed multiple times", "suite")) {
			if (test_opts.suites_len > MAX_CMDLINE_TEST_SUITES) {
				LOG_E("too many -s options (max: %d)", MAX_CMDLINE_TEST_SUITES);
				return false;
			}
			test_opts.suites[test_opts.suites_len] = opt_ctx.optarg;
			++test_opts.suites_len;
		} else if (opt_match(
				   'd', "change progress display mode", opt_match_enum_table(progress_display_table))) {
			test_opts.display = opt_ctx.optarg_enum_value;
		} else if (opt_match('o', "set output mode", opt_match_enum_table(output_table))) {
			test_opts.display = opt_ctx.optarg_enum_value;
		} else if (opt_match('f', "fail fast; exit after first failure")) {
			test_opts.fail_fast = true;
		} else if (opt_match('S', "print a summary with elapsed time")) {
			test_opts.print_summary = true;
		} else if (opt_match('j', "set the number of test workers", "jobs")) {
			char *endptr;
			unsigned long n = strtoul(opt_ctx.optarg, &endptr, 10);

			if (n > UINT32_MAX || !*opt_ctx.optarg || *endptr) {
				LOG_E("invalid number of jobs: %s", opt_ctx.optarg);
				return false;
			}

			test_opts.jobs = n;
		} else if (opt_match('v', "increase verbosity, may be passed twice")) {
			++test_opts.verbosity;
		} else if (opt_match('R', "disable automatic rebuild")) {
			test_opts.no_rebuild = true;
		} else if (opt_match('t', "multiply test timeouts with <factor>", "factor")) {
			char *endptr;
			test_opts.timeout_multiplier = strtof(opt_ctx.optarg, &endptr);

			if (!*opt_ctx.optarg || *endptr) {
				LOG_E("invalid timeout multiplier: %s", opt_ctx.optarg);
				return false;
			}
		}
	}
	opt_end();

	if (!ensure_in_build_dir()) {
		return false;
	}

	test_opts.tests = &argv[argi];
	test_opts.tests_len = argc - argi;

	return tests_run(wk, &test_opts, argv[0]);
}

static bool
cmd_install(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct install_options opts = {
		.destdir = os_get_env("DESTDIR"),
	};

	opt_for() {
		if (opt_match('n', "dry run")) {
			opts.dry_run = true;
		} else if (opt_match('d', "set destdir", "destdir")) {
			opts.destdir = opt_ctx.optarg;
		} else if (opt_match('U', "uninstall")) {
			opts.uninstall = true;
		}
	}
	opt_end();

	if (!ensure_in_build_dir()) {
		return false;
	}

	return install_run(wk, &opts);
}

static void
cmd_setup_help(struct workspace *wk)
{
	log_plain(log_info, "\n");

	struct list_options_opts list_opts = { 0 };
	list_options(wk, &list_opts);

	log_plain(log_info, "To see all options, including builtin options, use `muon options -a`.\n");
}

static void
make_argv0_absolute(struct workspace *wk, struct tstr *buf, char *const argv[])
{
	if (!path_is_basename(argv[0])) {
		path_make_absolute(wk, buf, argv[0]);
		((char **)argv)[0] = buf->buf;
	}
}

struct cmd_setup_common_ctx {
	uint32_t argi;
	int32_t n_operands;
	obj build_dir;
	bool cached;
	const char *usage;
};

static bool
cmd_setup_common(struct workspace *wk,
	uint32_t argc,
	uint32_t argi,
	char *const argv[],
	struct cmd_setup_common_ctx *ctx)
{
	TracyCZoneAutoS;

	struct arr preload_files = { 0 };

	if (!opt_gather_all_ctx.enabled) {
		workspace_init_runtime(wk);
		arr_init(wk->a_scratch, &preload_files, 8, const char *);
	}

	bool res = false;
	enum workspace_do_setup_flag flags = 0;
	const char *build = 0;

	uint32_t original_argi = argi + 1;

	opt_for(ctx->n_operands, .usage_post = ctx->usage, .extra_help = cmd_setup_help) {
		if (opt_match('#', "enable setup progress bar")) {
			log_progress_enable(wk);
		} else if (opt_match('D', "set options", "option>=<value")) {
			if (!parse_and_set_cmdline_option(wk, opt_ctx.optarg)) {
				goto ret;
			}
		} else if (opt_match('b', "set breakpoint", "breakpoint")) {
			vm_dbg_push_breakpoint_str(wk, opt_ctx.optarg);
		} else if (opt_match('w', "wipe all caches before setup")) {
			flags |= workspace_do_setup_flag_clear_cache;
			ctx->cached = false;
		} else if (opt_match('p', "preload <file>", "file")) {
			arr_push(wk->a_scratch, &preload_files, &opt_ctx.optarg);
		}
	}
	opt_end();

	if (ctx->n_operands < 0 && argc - argi < 1) {
		opt_check_operands(argc, argi, 1);
		return false;
	}

	build = argv[argi];

	// The following shenanigans are to support passing the source dir instead
	// of the build dir.  We decide that the passed dir is a source dir (and
	// the build dir is the current dir) if the current dir does not contain a
	// build file.
	TSTR(argv0);
	TSTR(new_cwd);
	TSTR(old_cwd);
	{
		path_copy_cwd(wk, &old_cwd);

		enum build_language _lang;
		if (!determine_build_file(wk, path_cwd(), &_lang, true)) {
			// fix argv0 here since if it is a relative path it will be
			// wrong after chdir
			make_argv0_absolute(wk, &argv0, argv);

			if (!path_chdir(wk, build)) {
				return false;
			}

			path_copy_cwd(wk, &new_cwd);
			wk->source_root = new_cwd.buf;
			build = old_cwd.buf;

			((const char **)argv)[argi] = build;
		}
	}

	++argi;

	if (!workspace_do_setup_prepare(wk, build, argv[0], argi - original_argi, &argv[original_argi], flags)) {
		goto ret;
	}

	if (ctx->cached) {
		TSTR(cmdline);
		path_join(wk, &cmdline, build, output_path.private_dir);
		path_push(wk, &cmdline, output_path.paths[output_path_cmdline].path);
		if (fs_file_exists(cmdline.buf)) {
			struct source src;
			if (!fs_read_entire_file(wk->a_scratch, cmdline.buf, &src)) {
				return false;
			}

			if (!init_global_options(wk)) {
				UNREACHABLE;
			}

			obj regen_cmd = join_args_shell(wk, ca_regenerate_build_command(wk, true));

			if (str_eql(&(struct str) { src.src, src.len }, get_str(wk, regen_cmd))) {
				L("command line has not changed -- not regenerating");
				res = true;
				goto ret;
			} else {
				L("command line has changed:");
				L("original: %s", src.src);
				L("new:      %s", get_str(wk, regen_cmd)->s);
				ctx->cached = false;
			}
		}
	}

	// Extract any relevant -D options that need to be handled very early.
	// Currently this is only vsenv.  These haven't been added to any options
	// dict yet so we need to manually scan the option_overrides array.
	struct {
		enum setup_platform_env_requirement vsenv_req;
	} opts = {
		.vsenv_req = setup_platform_env_requirement_auto,
	};
	{
		uint32_t i;
		for (i = 0; i < wk->option_overrides.len; ++i) {
			struct option_override *oo = arr_get(&wk->option_overrides, i);
			if (oo->proj) {
				continue;
			}

			const struct str *k = get_str(wk, oo->name);
			const struct str *v = get_str(wk, oo->val);

			if (str_eql(&STR("vsenv"), k)) {
				opts.vsenv_req = str_eql(&STR("true"), v) ? setup_platform_env_requirement_required :
									    setup_platform_env_requirement_skip;
			}
		}
	}

	setup_platform_env(wk, build, opts.vsenv_req);

	if (!workspace_do_setup(wk, &preload_files)) {
		goto ret;
	}

	res = true;
ret:
	ctx->build_dir = build ? make_str(wk, build) : 0;
	ctx->argi = argi;
	TracyCZoneAutoE;
	return res;
}

static bool
cmd_setup(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct cmd_setup_common_ctx ctx = { .n_operands = 1, .usage = " <build dir|source dir>" };
	return cmd_setup_common(wk, argc, argi, argv, &ctx);
}

static bool
cmd_build(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct cmd_setup_common_ctx ctx = { .n_operands = -1, .cached = true, .usage = " <build dir|source dir> [ninja options] [ninja targets]" };
	if (!cmd_setup_common(wk, argc, argi, argv, &ctx)) {
		return false;
	}

	obj args = make_obj(wk, obj_array);
	for (argi = ctx.argi; argi < argc; ++argi) {
		obj_array_push(wk, args, make_str(wk, argv[argi]));
	}

	TSTR(old_cwd);
	path_copy_cwd(wk, &old_cwd);

	if (!path_chdir(wk, get_str(wk, ctx.build_dir)->s)) {
		return false;
	}

	if (ctx.cached) {
		if (!options_load_from_option_info(wk)) {
			return false;
		}
	}

	bool ok = ninja_run(wk, args, 0, 0, 0);

	if (!path_chdir(wk, old_cwd.buf)) {
		return false;
	}

	return ok;
}

static bool
cmd_format(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		char *const *filenames;
		const char *cfg_path;
		bool in_place, check_only, editorconfig, print_failures;
	} opts = { 0 };

	opt_for(-1, .usage_post = " <file>[ <file>[...]]") {
		if (opt_match('i', "format files in-place")) {
			opts.in_place = true;
		} else if (opt_match('c', "read configuration from path", "path")) {
			opts.cfg_path = opt_ctx.optarg;
		} else if (opt_match('q', "exit with 1 if files would be modified by muon fmt")) {
			opts.check_only = true;
		} else if (opt_match('e', "respect .editorconfig configuration")) {
			opts.editorconfig = true;
		} else if (opt_match('l', "like -q but also print failing filenames")) {
			opts.check_only = true;
			opts.print_failures = true;
		}
	}
	opt_end();

	if (opts.in_place && opts.check_only) {
		LOG_E("-q and -i are mutually exclusive");
		return false;
	}

	log_set_file(wk, stderr);

	opts.filenames = &argv[argi];
	const uint32_t num_files = argc - argi;

	bool ret = true;
	bool opened_out;
	FILE *out;
	uint32_t i;
	for (i = 0; i < num_files; ++i) {
		bool fmt_ret = true;
		opened_out = false;

		struct source src = { 0 };
		if (!fs_read_entire_file(wk->a_scratch, opts.filenames[i], &src)) {
			ret = false;
			goto cont;
		}

		if (opts.in_place) {
			if (!(out = fs_fopen(opts.filenames[i], "wb"))) {
				ret = false;
				goto cont;
			}
			opened_out = true;
		} else if (opts.check_only) {
			out = NULL;
		} else {
			out = stdout;
		}

		workspace_scratch_begin(wk);
		workspace_perm_begin(wk);
		fmt_ret = fmt(wk->a, wk->a_scratch, &src, out, opts.cfg_path, opts.check_only, opts.editorconfig);
		workspace_perm_end(wk);
		workspace_scratch_end(wk);

		if (!fmt_ret && opts.print_failures) {
			printf("%s\n", opts.filenames[i]);
		}
cont:
		if (opened_out) {
			fs_fclose(out);

			if (!fmt_ret) {
				fs_write(opts.filenames[i], (const uint8_t *)src.src, src.len);
			}
		}
		ret &= fmt_ret;
	}

	return ret;
}

static bool
cmd_help(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *query;
		bool cli;
	} opts = { .cli = true };

	opt_for(-1, .usage_post = " [query]") {
		if (opt_match('r', "reference. get help for meson functions")) {
			opts.cli = false;
		}
	}
	opt_end();

	if (argi + 1 == argc) {
		opts.query = argv[argi];
	} else if (argi < argc) {
		return opt_check_operands(argc, argi, 1);
	}

	workspace_init_runtime(wk);
	workspace_init_startup_files(wk);
	make_dummy_project(wk, true);

	char tmp_path[512] = { 0 };
	FILE *tmp = 0;
	if (!(tmp = fs_make_tmp_file("help", "", tmp_path, sizeof(tmp_path)))) {
		return false;
	}

	struct dump_function_docs_opts dump_opts = {
		.type = dump_function_docs_output_man,
		.out = tmp,
		.query = opts.query,
	};

	if (opts.cli) {
		opt_gather_all(wk, cmd_main);
		const struct opt_gather_all_ctx *ga = &opt_gather_all_ctx;
		dump_cli_docs(wk, &dump_opts, &ga->commands);
	} else {
		dump_function_docs(wk, &dump_opts);
	}

	fs_fclose(tmp);
	tmp = 0;

	struct run_cmd_ctx cmd_ctx = { 0 };
	cmd_ctx.stdin_path = tmp_path;
	cmd_ctx.flags = run_cmd_ctx_flag_dont_capture;
	char *const mandoc_args[] = { "mandoc", "-a", NULL };
	bool ok = run_cmd_argv(wk, &cmd_ctx, mandoc_args, 0, 0);
	run_cmd_ctx_destroy(&cmd_ctx);

	if (tmp) {
		fs_fclose(tmp);
	}
	if (*tmp_path) {
		fs_remove(tmp_path);
	}
	return ok;
}

static bool
cmd_version(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	opt_for() {
	}
	opt_end();

	printf("muon %s%s%s\nmeson compatibility version %s\n",
		muon_version.version,
		*muon_version.vcs_tag ? "-" : "",
		muon_version.vcs_tag,
		muon_version.meson_compat);
	printf("compiled with: %s, for platform: %s, release build: %s\n",
		muon_version.compiler,
		muon_version.platform,
		MUON_RELEASE ? "yes" : "no");
	printf("enabled features:\n");

	const struct {
		const char *name;
		bool enabled;
	} feature_names[] = {
		{ "libcurl", have_libcurl },
		{ "libarchive", have_libarchive },
		{ "samurai", have_samurai },
#ifdef TRACY_ENABLE
		{ "tracy", true },
#endif
#ifdef __SANITIZE_ADDRESS__
		{ "asan", true },
#endif
#ifdef __SANITIZE_UNDEFINED__
		{ "ubsan", true },
#endif
#ifdef __SANITIZE_MEMORY__
		{ "msan", true },
#endif
		{ "native backtrace", have_platform_backtrace_capture },
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(feature_names); ++i) {
		if (feature_names[i].enabled) {
			printf("  %s\n", feature_names[i].name);
		}
	}

	muon_pkgconfig_init(0);

	for (i = 0; i < ARRAY_LEN(pkgconfig_impls); ++i) {
		if (pkgconfig_impls[i].get_variable) {
			printf("  pkgconfig:%s\n", muon_pkgconfig_impl_type_to_s(i));
		}
	}

	return true;
}

static bool
cmd_meson(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	const struct opt_gather_all_ctx *ga = &opt_gather_all_ctx;
	if (ga->enabled) {
		struct opt_gathered_command cmd = { 0 };
		cmd.usage_post = " ...";
		cmd.desc = "A compatibility layer that attempts to translate all flags and "
			   "operands from meson cli syntax to muon cli syntax.  For example, the "
			   "following two commands:\n"
			   "\n"
			   "```\n"
			   "muon meson setup build --werror --prefix=/\n"
			   "muon meson test -C build --list\n"
			   "```\n"
			   "\n"
			   "Would be translated into the following two muon versions respectively:\n"
			   "\n"
			   "```\n"
			   "muon setup -Dwerror=true -Dprefix=/ build\n"
			   "muon -C build test -l\n"
			   "```\n"
			   "\n"
			   "This compatibility layer is also enabled when muon's executable is named "
			   "_meson_.\n"
			   "\n"
			   "For more detailed usage information you can use the following two "
			   "commands:\n"
			   "\n"
			   "```\n"
			   "muon meson -h\n"
			   "muon meson <subcommand> -h\n"
			   "```\n"
			   "\n"
			   "NOTE: This is a best-effort translation and does not guarantee or imply "
			   "full cli compatibility.  Many unimplemented flags are ignored and "
			   "attempting to use an unsupported subcommands will result in an error.\n";
		opt_gather_all_push_custom(wk, &cmd);
		return false;
	}

	++argi;

	char **new_argv;
	uint32_t new_argc, new_argi;
	if (!translate_meson_opts(wk, argc, argi, (char **)argv, &new_argc, &new_argi, &new_argv)) {
		return false;
	}

	argi = new_argi;
	argc = new_argc;
	argv = new_argv;

	return cmd_main(wk, argc, argi, (char **)argv);
}

static bool
cmd_ui(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	opt_for() {
	}
	opt_end();

	return ui_main(wk);
}

static bool
cmd_devenv(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	opt_for(-1, .usage_post = " <command to run>") {
	}
	opt_end();

	if (argi >= argc) {
		LOG_E("missing command");
		return false;
	}

	if (!ensure_in_build_dir()) {
		return false;
	}

	setup_platform_env(wk, ".", setup_platform_env_requirement_from_cache);

	const char *const *cmd = (const char *const *)&argv[argi];

	struct run_cmd_ctx ctx = { 0 };
	ctx.flags |= run_cmd_ctx_flag_dont_capture;

	bool ok = true;
	if (!run_cmd_argv(wk, &ctx, (char *const *)cmd, 0, 0)) {
		LOG_E("failed to run command: %s", ctx.err_msg);
		ok = false;
	}

	exit(ok ? ctx.status : 1);
}

static bool
cmd_main(struct workspace *wk, uint32_t argc, uint32_t argi, char *const argv[])
{
	const struct opt_command commands[] = {
		{ "analyze", cmd_analyze, "run a static analyzer" },
		{ "build",
			cmd_build,
			"setup and build in a single step",
			"Invoke muon setup and build in a single command.  Additionally, setup will only "
			"run if the passed options have changed, including options implicitly set "
			"using environment variables."
		},
		{ "devenv", cmd_devenv, "run commands in developer environment" },
		{ "fmt", cmd_format, "format a meson source file" },
		{ "help", cmd_help, "get help" },
		{ "install", cmd_install, "install files" },
		{ "internal", cmd_internal, "internal subcommands" },
		{ "meson", cmd_meson, "meson cli compatibility layer" },
		{ "options", cmd_options, "list project options" },
		{ "samu", cmd_samu, have_samurai ? "run samurai" : NULL, .skip_gather = true },
		{ "setup", cmd_setup, "setup a build directory" },
		{ "subprojects", cmd_subprojects, "manage subprojects" },
		{ "test", cmd_test, "run tests" },
		{ "ui", cmd_ui, have_ui ? "run an interactive ui" : NULL },
		{ "version", cmd_version, "print version information" },
		{ 0 },
	};

	TSTR(argv0);

	opt_for(-1, commands) {
		if (opt_match('v', "turn on debug messages")) {
			log_set_lvl(log_debug);
		} else if (opt_match('q', "silence logging except for errors")) {
			log_set_lvl(log_error);
		} else if (opt_match('C', "chdir to path", "path")) {
			// fix argv0 here since if it is a relative path it will be
			// wrong after chdir
			make_argv0_absolute(wk, &argv0, argv);

			if (!path_chdir(wk, opt_ctx.optarg)) {
				return false;
			}
		}
	}
	opt_end();

	uint32_t cmd_i;
	if (!opt_find_cmd(commands, &cmd_i, argc, argi, argv, false)) {
		return false;
	}

	return commands[cmd_i].cmd(wk, argc, argi, argv);
}

static void
signal_handler(int signal, const char *signal_name, void *_ctx)
{
	struct workspace *wk = _ctx;

	LOG_I("caught signal %d (%s)", signal, signal_name);

	struct platform_backtrace bt = { 0 };
	platform_backtrace_capture(wk->a, &bt);

	LOG_I("native backtrace (%d frames):", bt.frames.len);
	for (uint32_t i = 0; i < bt.frames.len; i++) {
		const struct platform_backtrace_frame *frame = arr_get(&bt.frames, i);
		LOG_I("%p <%s+%d> at %s", frame->addr, frame->symbol_name, (int)frame->offset, frame->file_name);
	}

	log_flush();

	if (wk->vm.run) {
		vm_error(wk, "encountered unhandled runtime error");
	} else if (wk->backend_output_stack) {
		LOG_E("an unhandled error occured during backend output");
		backend_print_stack(wk);
	}

	log_flush();
}

int
main(int argc, char *argv[])
{
	platform_init();

	struct arena a;
	struct arena a_scratch;
	arena_init(&a, );
	arena_init(&a_scratch, );
	struct workspace wk;
	workspace_init_arena(&wk, &a, &a_scratch);

	platform_set_signal_handler(signal_handler, &wk);

	log_set_file(&wk, stdout);
	log_set_lvl(log_info);

	workspace_init_bare(&wk, &a, &a_scratch);

	path_init(&wk);

	machine_init();

	bool res;
	bool meson_compat = false;

	{
		TSTR(basename);
		path_basename(&wk, &basename, argv[0]);
		meson_compat = strcmp(basename.buf, "meson") == 0 && (argc < 2 || strcmp(argv[1], "internal") != 0);
	}

	if (meson_compat) {
		res = cmd_meson(&wk, argc, 0, argv);
	} else {
		res = cmd_main(&wk, argc, 0, argv);
	}

	int ret = res ? 0 : 1;

#if !MUON_RELEASE
	ar_destroy(&a);
	ar_destroy(&a_scratch);
#endif

#ifdef TRACY_ENABLE
	sleep(1);
#endif
	return ret;
}

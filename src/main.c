#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "backend/backend.h"
#include "cmd_install.h"
#include "cmd_test.h"
#include "embedded.h"
#include "external/libarchive.h"
#include "external/libcurl.h"
#include "external/libpkgconf.h"
#include "external/samurai.h"
#include "lang/analyze.h"
#include "lang/fmt.h"
#include "lang/interpreter.h"
#include "lang/serial.h"
#include "machine_file.h"
#include "options.h"
#include "opts.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "version.h"
#include "wrap.h"

static bool
ensure_in_build_dir(void)
{
	if (!fs_dir_exists("muon-private")) {
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
	if (!(f = fs_fopen(path, "r"))) {
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
cmd_exe(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *feed;
		const char *capture;
		const char *environment;
		const char *args;
		const char *const *cmd;
	} opts = { 0 };

	OPTSTART("f:c:e:a:") {
		case 'f':
			opts.feed = optarg;
			break;
		case 'c':
			opts.capture = optarg;
			break;
		case 'e':
			opts.environment = optarg;
			break;
		case 'a':
			opts.args = optarg;
			break;
	} OPTEND(argv[argi],
		" <cmd> [arg1[ arg2[...]]]",
		"  -f <file> - feed file to input\n"
		"  -c <file> - capture output to file\n"
		"  -e <file> - load environment from data file\n"
		"  -a <file> - load arguments from data file\n",
		NULL, -1)

	if (argi >= argc && !opts.args) {
		LOG_E("missing command");
		return false;
	} else if (argi < argc && opts.args) {
		LOG_E("command cannot be specified by trailing arguments *and* -a");
		return false;
	}

	opts.cmd = (const char *const *)&argv[argi];
	++argi;

	bool ret = false;
	struct run_cmd_ctx ctx = { 0 };
	ctx.stdin_path = opts.feed;

	if (!opts.capture) {
		ctx.flags |= run_cmd_ctx_flag_dont_capture;
	}

	struct workspace wk;
	bool initialized_workspace = false,
	     allocated_argv = false;

	const char *envstr = NULL;
	uint32_t envc = 0;
	if (opts.environment) {
		initialized_workspace = true;
		workspace_init_bare(&wk);

		obj env;
		if (!load_obj_from_serial_dump(&wk, opts.environment, &env)) {
			goto ret;
		}

		env_to_envstr(&wk, &envstr, &envc, env);
	}

	if (opts.args) {
		if (!initialized_workspace) {
			initialized_workspace = true;
			workspace_init_bare(&wk);
		}

		obj args;
		if (!load_obj_from_serial_dump(&wk, opts.args, &args)) {
			goto ret;
		}

		const char *argstr;
		uint32_t argc;
		join_args_argstr(&wk, &argstr, &argc, args);

		argstr_to_argv(argstr, argc, NULL, (char *const **)&opts.cmd);
		allocated_argv = true;
	}

	if (!run_cmd_argv(&ctx, opts.cmd[0], (char *const *)opts.cmd, envstr, envc)) {
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
	if (initialized_workspace) {
		workspace_destroy_bare(&wk);
	}
	if (allocated_argv) {
		z_free((void *)opts.cmd);
	}
	return ret;
}

static bool
cmd_check(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *filename;
		bool print_ast;
	} opts = { 0 };

	OPTSTART("p") {
		case 'p':
			opts.print_ast = true;
			break;
	} OPTEND(argv[argi],
		" <filename>",
		"  -p - print parsed ast\n",
		NULL, 1)

	opts.filename = argv[argi];

	bool ret = false;

	struct source src = { 0 };
	struct ast ast = { 0 };
	struct source_data sdata = { 0 };

	if (!fs_read_entire_file(opts.filename, &src)) {
		goto ret;
	}

	if (!parser_parse(&ast, &sdata, &src, 0)) {
		goto ret;
	}

	if (opts.print_ast) {
		print_ast(&ast);
	}

	ret = true;
ret:
	fs_source_destroy(&src);
	ast_destroy(&ast);
	source_data_destroy(&sdata);
	return ret;
}

static bool
cmd_analyze(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct analyze_opts opts = {
		.replay_opts = error_diagnostic_store_replay_include_sources,
	};

	OPTSTART("luqO:") {
		case 'l':
			opts.subdir_error = true;
			opts.replay_opts &= ~error_diagnostic_store_replay_include_sources;
			break;
		case 'O':
			opts.file_override = optarg;
			break;
		case 'u':
			opts.unused_variable_error = true;
			break;
		case 'q':
			opts.replay_opts |= error_diagnostic_store_replay_errors_only;
			break;
	} OPTEND(argv[argi], "",
		"  -l - optimize output for editor linter plugins\n"
		"  -q - only report errors\n"
		"  -u - error on unused variables\n"
		"  -O <path> - read project file with matching path from stdin\n"
		,
		NULL, 0)

	return do_analyze(&opts);
}

static bool
cmd_options(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct list_options_opts opts = { 0 };
	OPTSTART("am") {
		case 'a':
			opts.list_all = true;
			break;
		case 'm':
			opts.only_modified = true;
			break;
	} OPTEND(argv[argi], "",
		"  -a - list all options"
		"  -m - list only modified options"
		,
		NULL, 0)

	return list_options(&opts);
}

static bool
cmd_summary(uint32_t argc, uint32_t argi, char *const argv[])
{
	OPTSTART("") {
	} OPTEND(argv[argi], "", "", NULL, 0)

	if (!ensure_in_build_dir()) {
		return false;
	}

	bool ret = false;
	struct source src = { 0 };
	if (!fs_read_entire_file("muon-private/summary.txt", &src)) {
		goto ret;
	}

	fwrite(src.src, 1, src.len, stdout);

	ret = true;
ret:
	fs_source_destroy(&src);
	return ret;
}

static bool
cmd_info(uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "options", cmd_options, "list project options" },
		{ "summary", cmd_summary, "print a configured project's summary" },
		0,
	};

	OPTSTART("") {
	} OPTEND(argv[argi], "", "", commands, -1);

	cmd_func cmd = NULL;;
	if (!find_cmd(commands, &cmd, argc, argi, argv, false)) {
		return false;
	}

	assert(cmd);
	return cmd(argc, argi, argv);
}

static const char *cmd_subprojects_subprojects_dir = "subprojects";

static bool
cmd_subprojects_check_wrap(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *filename;
	} opts = { 0 };

	OPTSTART("") {
	} OPTEND(argv[argi],
		" <filename>",
		"",
		NULL, 1)

	opts.filename = argv[argi];

	bool ret = false;

	struct wrap wrap = { 0 };
	if (!wrap_parse(opts.filename, &wrap)) {
		goto ret;
	}

	ret = true;
ret:
	wrap_destroy(&wrap);
	return ret;
}

struct cmd_subprojects_download_ctx {
	const char *subprojects;
};

static enum iteration_result
cmd_subprojects_download_iter(void *_ctx, const char *name)
{
	struct cmd_subprojects_download_ctx *ctx = _ctx;
	uint32_t len = strlen(name);
	SBUF_1k(path, sbuf_flag_overflow_alloc);

	if (len <= 5 || strcmp(&name[len - 5], ".wrap") != 0) {
		goto cont;
	}

	path_join(NULL, &path, ctx->subprojects, name);

	if (!fs_file_exists(path.buf)) {
		goto cont;
	}

	LOG_I("fetching %s", name);
	struct wrap wrap = { 0 };
	if (!wrap_handle(path.buf, ctx->subprojects, &wrap, true)) {
		goto cont;
	}

	wrap_destroy(&wrap);
cont:
	sbuf_destroy(&path);
	return ir_cont;
}

static bool
cmd_subprojects_download(uint32_t argc, uint32_t argi, char *const argv[])
{
	OPTSTART("") {
	} OPTEND(argv[argi], " <list of subprojects>", "", NULL, -1)

	char path[PATH_MAX];
	if (!path_make_absolute(path, PATH_MAX, cmd_subprojects_subprojects_dir)) {
		return false;
	}

	struct cmd_subprojects_download_ctx ctx = {
		.subprojects = path,
	};

	if (argc > argi) {
		SBUF_1k(wrap_file, sbuf_flag_overflow_alloc);

		for (; argc > argi; ++argi) {
			path_join(NULL, &wrap_file, ctx.subprojects, argv[argi]);

			sbuf_pushs(NULL, &wrap_file, ".wrap");

			if (!fs_file_exists(wrap_file.buf)) {
				LOG_E("wrap file for '%s' not found", argv[argi]);
				return false;
			}

			if (cmd_subprojects_download_iter(&ctx, wrap_file.buf) == ir_err) {
				return false;
			}
		}

		sbuf_destroy(&wrap_file);
		return true;
	} else {
		return fs_dir_foreach(path, &ctx, cmd_subprojects_download_iter);
	}
}

static bool
cmd_subprojects(uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "check-wrap", cmd_subprojects_check_wrap, "check if a wrap file is valid" },
		{ "download", cmd_subprojects_download, "download subprojects" },
		{ 0 },
	};

	OPTSTART("d:") {
		case 'd':
			cmd_subprojects_subprojects_dir = optarg;
			break;
	} OPTEND(argv[0], "",
		"  -d <directory> - use an alternative subprojects directory\n",
		commands, -1)

	cmd_func cmd;
	if (!find_cmd(commands, &cmd, argc, argi, argv, false)) {
		return false;
	}

	return cmd(argc, argi, argv);
}

static bool
eval_internal(const char *filename, bool embedded, const char *argv0, char *const argv[], uint32_t argc)
{
	bool ret = false;

	struct source src = { 0 };
	bool src_allocd = false;

	struct workspace wk;
	workspace_init(&wk);

	wk.lang_mode = language_internal;

	if (embedded) {
		if (!(src.src = embedded_get(filename))) {
			LOG_E("failed to find '%s' in embedded sources", filename);
			goto ret;
		}

		src.len = strlen(src.src);
		src.label = filename;
	} else {
		if (!fs_read_entire_file(filename, &src)) {
			goto ret;
		}
		src_allocd = true;
	}

	uint32_t proj_id;
	make_project(&wk, &proj_id, "dummy", wk.source_root, wk.build_root);

	{ // populate argv array
		obj argv_obj;
		make_obj(&wk, &argv_obj, obj_array);
		hash_set_str(&wk.scope, "argv", argv_obj);

		uint32_t i;
		for (i = 0; i < argc; ++i) {
			obj_array_push(&wk, argv_obj, make_str(&wk, argv[i]));
		}
	}

	obj res;
	if (!eval(&wk, &src, eval_mode_default, &res)) {
		goto ret;
	}

	ret = true;
ret:
	if (src_allocd) {
		fs_source_destroy(&src);
	}
	workspace_destroy(&wk);
	return ret;
}

static bool
cmd_eval(uint32_t argc, uint32_t argi, char *const argv[])
{
	const char *filename;
	bool embedded = false;

	OPTSTART("e") {
		case 'e':
			embedded = true;
			break;
	} OPTEND(argv[argi], " <filename> [args]", "", NULL, -1)

	if (argi >= argc) {
		LOG_E("missing required filename argument");
		return false;
	}

	filename = argv[argi];

	return eval_internal(filename, embedded, argv[0], &argv[argi], argc - argi);
}

static bool
cmd_repl(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace wk;
	workspace_init(&wk);
	wk.lang_mode = language_internal;

	obj id;
	make_project(&wk, &id, "dummy", wk.source_root, wk.build_root);

	repl(&wk, false);

	workspace_destroy(&wk);
	return true;
}

static bool
cmd_internal(uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "eval", cmd_eval, "evaluate a file" },
		{ "exe", cmd_exe, "run an external command" },
		{ "repl", cmd_repl, "start a meson language repl" },
		0,
	};

	OPTSTART("") {
	} OPTEND(argv[argi], "", "", commands, -1);

	cmd_func cmd = NULL;;
	if (!find_cmd(commands, &cmd, argc, argi, argv, false)) {
		return false;
	}

	assert(cmd);
	return cmd(argc, argi, argv);
}

static bool
cmd_samu(uint32_t argc, uint32_t argi, char *const argv[])
{
	return muon_samu(argc - argi, (char **)&argv[argi]);
}

static bool
cmd_test(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct test_options test_opts = { 0 };

	if (strcmp(argv[argi], "benchmark") == 0) {
		test_opts.cat = test_category_benchmark;
		test_opts.print_summary = true;
	}

	OPTSTART("s:d:Sfw:vR") {
		case 's':
			if (test_opts.suites_len > MAX_CMDLINE_TEST_SUITES) {
				LOG_E("too many -s options (max: %d)", MAX_CMDLINE_TEST_SUITES);
				return false;
			}
			test_opts.suites[test_opts.suites_len] = optarg;
			++test_opts.suites_len;
			break;
		case 'd':
			if (strcmp(optarg, "auto") == 0) {
				test_opts.display = test_display_auto;
			} else if (strcmp(optarg, "dots") == 0) {
				test_opts.display = test_display_dots;
			} else if (strcmp(optarg, "bar") == 0) {
				test_opts.display = test_display_bar;
			} else {
				LOG_E("invalid progress display mode '%s'", optarg);
				return false;
			}
			break;
		case 'f':
			test_opts.fail_fast = true;
			break;
		case 'S':
			test_opts.print_summary = true;
			break;
		case 'w': {
			char *endptr;
			unsigned long n = strtoul(optarg, &endptr, 10);

			if (n > UINT32_MAX || !*optarg || *endptr) {
				LOG_E("invalid number of workers: %s", optarg);
				return false;
			}

			test_opts.workers = n;
			break;
		}
		case 'v':
			++test_opts.verbosity;
			break;
		case 'R':
			test_opts.no_rebuild = true;
			break;
	} OPTEND(argv[argi], "",
		"  -s <suite> - only run items in <suite>, may be passed multiple times\n"
		"  -d <mode> - change progress display mode (auto|dots|bar)\n"
		"  -S - print a summary with elapsed time\n"
		"  -f - fail fast; exit after first failure\n"
		"  -w <workers> - set the number of test workers\n"
		"  -v - increase verbosity, may be passed twice\n"
		"  -R - disable automatic rebuild\n",
		NULL, 0)

	if (!ensure_in_build_dir()) {
		return false;
	}

	return tests_run(&test_opts);
}

static bool
cmd_install(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct install_options opts = { 0 };

	OPTSTART("n") {
		case 'n':
			opts.dry_run = true;
			break;
	} OPTEND(argv[argi], "",
		"  -n - dry run\n",
		NULL, 0)

	if (!ensure_in_build_dir()) {
		return false;
	}

	return install_run(&opts);
}

static bool
cmd_setup(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace wk;
	workspace_init(&wk);

	uint32_t original_argi = argi + 1;

	OPTSTART("D:c:b") {
		case 'D':
			if (!parse_and_set_cmdline_option(&wk, optarg)) {
				goto err;
			}
			break;
		case 'c': {
			FILE *f;
			if (!(f = fs_fopen(optarg, "r"))) {
				goto err;
			} else if (!serial_load(&wk, &wk.compiler_check_cache, f)) {
				LOG_E("failed to load compiler check cache");
				goto err;
			} else if (!fs_fclose(f)) {
				goto err;
			}
			break;
		}
		case 'b':
			wk.dbg.break_on_err = true;
			break;
	} OPTEND(argv[argi],
		" <build dir>",
		"  -D <option>=<value> - set project options\n"
		"  -c <compiler_check_cache.dat> - path to compiler check cache dump\n"
		"  -b - break on errors\n",
		NULL, 1)

	const char *build = argv[argi];
	++argi;

	if (!workspace_setup_paths(&wk, build, argv[0],
		argc - original_argi,
		&argv[original_argi])) {
		goto err;
	}

	uint32_t project_id;
	if (!eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id)) {
		goto err;
	}

	log_plain("\n");

	if (!backend_output(&wk)) {
		goto err;
	}

	workspace_print_summaries(&wk, log_file());

	LOG_I("setup complete");

	workspace_destroy(&wk);
	return true;
err:
	workspace_destroy(&wk);
	return false;
}

static bool
cmd_format(uint32_t argc, uint32_t argi, char *const argv[])
{
	if (strcmp(argv[argi], "fmt_unstable") == 0) {
		LOG_W("the subcommand name fmt_unstable is deprecated, please use fmt instead");
	}

	struct {
		const char *filename;
		const char *cfg_path;
		bool in_place;
	} opts = { 0 };

	OPTSTART("ic:") {
		case 'i':
			opts.in_place = true;
			break;
		case 'c':
			opts.cfg_path = optarg;
			break;
	} OPTEND(argv[argi], " <filename>",
		"  -i - modify file in-place\n"
		"  -c <cfg.ini> - read configuration from cfg\n",
		NULL, 1)

	opts.filename = argv[argi];

	bool opened_out = false;
	bool ret = false;

	struct source src = { 0 };
	struct ast ast = { 0 };
	struct source_data sdata = { 0 };

	if (!fs_read_entire_file(opts.filename, &src)) {
		goto ret;
	} else if (!parser_parse(&ast, &sdata, &src, pm_keep_formatting)) {
		goto ret;
	}

	FILE *out;

	if (opts.in_place) {
		if (!(out = fs_fopen(opts.filename, "w"))) {
			goto ret;
		}
		opened_out = true;
	} else {
		out = stdout;
	}

	if (!fmt(&ast, out, opts.cfg_path)) {
		goto ret;
	}

	ret = true;
ret:
	if (opened_out) {
		fs_fclose(out);
	}
	fs_source_destroy(&src);
	ast_destroy(&ast);
	source_data_destroy(&sdata);
	return ret;
}

static bool
cmd_version(uint32_t argc, uint32_t argi, char *const argv[])
{
	printf("muon v%s%s%s\nmeson compatibility version %s\nenabled features:",
		muon_version.version,
		*muon_version.vcs_tag ? "-" : "",
		muon_version.vcs_tag,
		muon_version.meson_compat
		);
	if (have_libcurl) {
		printf(" libcurl");
	}

	if (have_libpkgconf) {
		printf(" libpkgconf");
	}

	if (have_libarchive) {
		printf(" libarchive");
	}

	if (have_samurai) {
		printf(" samurai");
	}

	printf("\n");
	return true;
}

static bool
cmd_main(uint32_t argc, uint32_t argi, char *argv[])
{
	static const struct command commands[] = {
		{ "analyze", cmd_analyze, "run a static analyzer on the current project." },
		{ "benchmark", cmd_test, "run benchmarks" },
		{ "check", cmd_check, "check if a meson file parses" },
		{ "fmt_unstable", cmd_format, NULL },
		{ "fmt", cmd_format, "format meson source file" },
		{ "info", cmd_info, "display project information" },
		{ "install", cmd_install, "install project" },
		{ "internal", cmd_internal, "internal subcommands" },
		{ "samu", cmd_samu, "run samurai" },
		{ "setup", cmd_setup, "setup a build directory" },
		{ "subprojects", cmd_subprojects, "manage subprojects" },
		{ "test", cmd_test, "run tests" },
		{ "version", cmd_version, "print version information" },
		{ 0 },
	};

	OPTSTART("vlC:") {
		case 'v':
			log_set_lvl(log_debug);
			break;
		case 'l':
			log_set_opts(log_show_source);
			break;
		case 'C': {
			// fix argv0 here since if it is a relative path it will be
			// wrong after chdir
			static char argv0[PATH_MAX];
			if (!path_is_basename(argv[0])) {
				if (!path_make_absolute(argv0, PATH_MAX, argv[0])) {
					return false;
				}

				argv[0] = argv0;
			}

			if (!path_chdir(optarg)) {
				return false;
			}
			break;
		}
	} OPTEND(argv[0], "",
		"  -v - turn on debug messages\n"
		"  -l - show source locations for log messages\n"
		"  -C <path> - chdir to path\n",
		commands, -1)

	cmd_func cmd;
	if (!find_cmd(commands, &cmd, argc, argi, argv, false)) {
		return false;
	}

	return cmd(argc, argi, argv);
}

int
main(int argc, char *argv[])
{
	log_init();
	log_set_lvl(log_info);

	path_init();

	compilers_init();

	int ret = cmd_main(argc, 0, argv) ? 0 : 1;

	path_deinit();
	return ret;
}

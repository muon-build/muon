#include "posix.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "backend/ninja.h"
#include "compilers.h"
#include "embedded.h"
#include "external/libarchive.h"
#include "external/libcurl.h"
#include "external/libpkgconf.h"
#include "external/samurai.h"
#include "formats/ini.h"
#include "functions/default/options.h"
#include "functions/default/setup.h"
#include "install.h"
#include "lang/eval.h"
#include "lang/fmt.h"
#include "lang/interpreter.h"
#include "lang/serial.h"
#include "log.h"
#include "machine_file.h"
#include "opts.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "tests.h"
#include "version.h"
#include "wrap.h"

static bool
load_environment_from_serial_dump(struct workspace *wk, const char *path, char *const *envp[])
{
	bool ret = false;
	FILE *f;
	if (!(f = fs_fopen(path, "r"))) {
		return false;
	}

	obj env;
	if (!serial_load(wk, &env, f)) {
		LOG_E("failed to load environment data");
		goto ret;
	}

	if (!env_to_envp(wk, 0, envp, env)) {
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
		const char *const *cmd;
	} opts = { 0 };

	OPTSTART("f:c:e:") {
		case 'f':
			opts.feed = optarg;
			break;
		case 'c':
			opts.capture = optarg;
			break;
		case 'e':
			opts.environment = optarg;
			break;
	} OPTEND(argv[argi],
		" <cmd> [arg1[ arg2[...]]]",
		"  -f <file> - feed file to input\n"
		"  -c <file> - capture output to file\n"
		"  -e <file> - load environemnt from data file\n",
		NULL, -1)

	if (argi >= argc) {
		LOG_E("missing command");
		return false;
	}

	opts.cmd = (const char *const *)&argv[argi];
	++argi;

	bool ret = false;
	struct run_cmd_ctx ctx = { 0 };
	ctx.stdin_path = opts.feed;

	struct workspace wk;
	bool initialized_workspace = false;
	char *const *envp = NULL;
	if (opts.environment) {
		initialized_workspace = true;
		workspace_init_bare(&wk);

		if (!load_environment_from_serial_dump(&wk, opts.environment, &envp)) {
			goto ret;
		}
	}

	if (!run_cmd(&ctx, opts.cmd[0], opts.cmd, envp)) {
		LOG_E("failed to run command: %s", ctx.err_msg);
		goto ret;
	}

	if (ctx.status != 0) {
		fputs(ctx.err.buf, stderr);
		goto ret;
	}

	if (opts.capture) {
		ret = fs_write(opts.capture, (uint8_t *)ctx.out.buf, ctx.out.len);
	} else {
		fputs(ctx.out.buf, stdout);
		ret = true;
	}
ret:
	run_cmd_ctx_destroy(&ctx);
	if (initialized_workspace) {
		workspace_destroy_bare(&wk);
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
	char path[PATH_MAX];

	if (len <= 5 || strcmp(&name[len - 5], ".wrap") != 0) {
		return ir_cont;
	}

	if (!path_join(path, PATH_MAX, ctx->subprojects, name)) {
		return ir_err;
	} else if (!fs_file_exists(path)) {
		return ir_cont;
	}

	LOG_I("fetching %s", name);
	struct wrap wrap = { 0 };
	if (!wrap_handle(path, ctx->subprojects, &wrap, true)) {
		return ir_err;
	}

	wrap_destroy(&wrap);
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
		char wrap_file[PATH_MAX];

		for (; argc > argi; ++argi) {
			if (!path_join(wrap_file, PATH_MAX, ctx.subprojects, argv[argi])) {
				return false;
			} else if (!path_add_suffix(wrap_file, PATH_MAX, ".wrap")) {
				return false;
			} else if (!fs_file_exists(wrap_file)) {
				LOG_E("wrap file for '%s' not found", argv[argi]);
				return false;
			}

			if (cmd_subprojects_download_iter(&ctx, wrap_file) == ir_err) {
				return false;
			}
		}
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

	if (!workspace_setup_dirs(&wk, "dummy", argv0, false)) {
		goto ret;
	}

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
	if (!eval(&wk, &src, &res)) {
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
	char buf[2048];
	bool ret = false;
	struct source src = { .label = "repl", .src = buf };
	struct source_data sdata = { 0 };
	struct ast ast = { 0 };

	struct workspace wk;
	workspace_init(&wk);
	wk.src = &src;
	wk.ast = &ast;

	wk.lang_mode = language_internal;

	obj id;
	make_project(&wk, &id, "dummy", wk.source_root, wk.build_root);

	fputs("> ", stderr);
	while (fgets(buf, 2048, stdin)) {
		src.len = strlen(buf);

		if (!parser_parse(&ast, &sdata, &src,
			pm_ignore_statement_with_no_effect)) {
			goto cont;
		}

		if (!interp_node(&wk, wk.ast->root, &id)) {
			goto cont;
		}

		if (id) {
			obj_fprintf(&wk, stderr, "%o\n", id);
			hash_set_str(&wk.scope, "_", id);
		}
cont:
		ast_destroy(&ast);
		fputs("> ", stderr);
	}

	ret = true;
	workspace_destroy(&wk);
	return ret;
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
	return muon_samu(argc - argi, (char **)&argv[argi]) == 0;
}

static bool
cmd_test(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct test_options test_opts = { 0 };

	OPTSTART("s:d:f") {
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
				LOG_E("invalid display option '%s'", optarg);
				return false;
			}
			break;
		case 'f':
			test_opts.fail_fast = true;
			break;
	} OPTEND(argv[argi], "",
		"  -s <suite> - only run tests in <suite>, may be passed multiple times\n"
		"  -d <display> - change how tests are displayed (auto|dots|bar)\n"
		"  -f - fail fast; exit after first failure\n",
		NULL, 0)

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

	return install_run(&opts);
}

static bool
cmd_setup(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace wk;
	workspace_init(&wk);

	OPTSTART("D:m:") {
		case 'D':
			if (!parse_and_set_cmdline_option(&wk, optarg)) {
				return false;
			}
			break;
		case 'm':
			if (!machine_file_parse(&wk, optarg)) {
				return false;
			}
			break;
	} OPTEND(argv[argi],
		" <build dir>",
		"  -D <option>=<value> - set project options\n",
		NULL, 1)

	const char *build = argv[argi];
	++argi;

	if (!workspace_setup_dirs(&wk, build, argv[0], true)) {
		goto err;
	}

	if (!do_setup(&wk)) {
		goto err;
	}

	workspace_destroy(&wk);
	return true;
err:
	workspace_destroy(&wk);
	return false;
}

static bool
cmd_auto(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *cfg;
	} opts = { .cfg = ".muon" };

	OPTSTART("c:rf") {
		case 'r':
			// HACK this should be redesigned as soon as more than one
			// function has command-line controllable behaviour.
			func_setup_flags |= func_setup_flag_force;
			func_setup_flags |= func_setup_flag_no_build;
			break;
		case 'f':
			// HACK this should be redesigned as soon as more than one
			// function has command-line controllable behaviour.
			func_setup_flags |= func_setup_flag_force;
			break;
		case 'c':
			opts.cfg = optarg;
			break;
	} OPTEND(argv[argi], "",
		"  -c config - load config alternate file (default: .muon)\n"
		"  -f - regenerate build file and rebuild\n"
		"  -r - regenerate build file only\n",
		NULL, 0)

	return eval_internal(opts.cfg, false, argv[0], NULL, 0);
}

static bool
cmd_format(uint32_t argc, uint32_t argi, char *const argv[])
{
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
	printf("muon v%s%s%s\nenabled features:",
		muon_version.version,
		*muon_version.vcs_tag ? "-" : "",
		muon_version.vcs_tag);
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
		{ "auto", cmd_auto, "build the project with options from a .muon file" },
		{ "check", cmd_check, "check if a meson file parses" },
		{ "fmt_unstable", cmd_format, "format meson source file" },
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
			// fig argv0 here since if it is a relative path it will be
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

	if (!path_init()) {
		return 1;
	}

	compilers_init();

	return cmd_main(argc, 0, argv) ? 0 : 1;
}

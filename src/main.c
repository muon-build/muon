#include "posix.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "compilers.h"
#include "external/curl.h"
#include "external/pkgconf.h"
#include "external/samu.h"
#include "external/zlib.h"
#include "formats/ini.h"
#include "functions/default/setup.h"
#include "lang/eval.h"
#include "log.h"
#include "machine_file.h"
#include "opts.h"
#include "output/output.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "tests.h"
#include "version.h"

static bool
cmd_exe(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *capture;
		char *const *cmd;
	} opts = { 0 };

	OPTSTART("c:") {
	case 'c':
		opts.capture = optarg;
		break;
	} OPTEND(argv[argi],
		" <cmd> [arg1[ arg2[...]]]",
		"  -c <file> - capture output to file\n",
		NULL)


	if (argi >= argc) {
		LOG_E("missing command");
		return false;
	}

	opts.cmd = &argv[argi];
	++argi;

	struct run_cmd_ctx ctx = { 0 };
	if (!run_cmd(&ctx, opts.cmd[0], opts.cmd)) {
		return false;
	}

	if (ctx.status != 0) {
		fputs(ctx.err, stderr);
		return false;
	}

	if (opts.capture) {
		return fs_write(opts.capture, (uint8_t *)ctx.out, strlen(ctx.out));
	} else {
		fputs(ctx.out, stdout);
		return true;
	}
}

static const char *
get_filename_as_only_arg(uint32_t argc, uint32_t argi, char *const argv[])
{
	OPTSTART("") {
	} OPTEND(argv[argi], " <filename>", "", NULL)

	if (argi >= argc) {
		LOG_E("missing filename");
		return NULL;
	}

	return argv[argi];
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
		"-p - print parsed ast\n",
		NULL)

	if (argi >= argc) {
		LOG_E("missing filename");
		return NULL;
	}

	opts.filename = argv[argi];

	bool ret = false;

	struct source src = { 0 };
	struct ast ast = { 0 };
	struct source_data sdata = { 0 };

	if (!fs_read_entire_file(opts.filename, &src)) {
		goto ret;
	}

	if (!parser_parse(&ast, &sdata, &src)) {
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
eval_internal(const char *filename, const char *argv0)
{
	bool ret = false;

	struct workspace wk;
	workspace_init(&wk);

	if (!workspace_setup_dirs(&wk, "dummy", argv0)) {
		goto ret;
	}

	wk.lang_mode = language_internal;

	struct source src = { 0 };

	if (!fs_read_entire_file(filename, &src)) {
		goto ret;
	}

	uint32_t id;
	make_project(&wk, &id, "dummy", wk.source_root, wk.build_root);

	uint32_t res;
	if (!eval(&wk, &src, &res)) {
		goto ret;
	}

	ret = true;
ret:
	fs_source_destroy(&src);
	workspace_destroy(&wk);
	return ret;
}

static bool
cmd_eval(uint32_t argc, uint32_t argi, char *const argv[])
{
	const char *filename;

	if (!(filename = get_filename_as_only_arg(argc, argi, argv))) {
		return false;
	}

	return eval_internal(filename, argv[0]);
}

static bool
cmd_repl(uint32_t argc, uint32_t argi, char *const argv[])
{
	char buf[2048];
	bool ret = false;
	struct source src = { .label = "repl", .src = buf };

	struct workspace wk;
	workspace_init(&wk);

	wk.lang_mode = language_internal;

	uint32_t id;
	make_project(&wk, &id, "dummy", wk.source_root, wk.build_root);

	fputs("> ", stderr);
	while (fgets(buf, 2048, stdin)) {
		src.len = strlen(buf);

		uint32_t res;
		if (eval(&wk, &src, &res)) {
			if (res) {
				obj_fprintf(&wk, stderr, "%o\n", res);
			}
		}
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
		{ "exe", cmd_exe, "run an external command" },
		{ "eval", cmd_eval, "evaluate a file" },
		{ "repl", cmd_repl, "start a meson langauge repl" },
		0,
	};

	OPTSTART("") {
	} OPTEND(argv[argi], "", "", commands);

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
	OPTSTART("") {
	} OPTEND(argv[argi], " <build dir>", "", NULL)

	if (argi >= argc) {
		LOG_E("missing build dir");
		return false;
	}

	return tests_run(argv[argi]);
}

static bool
cmd_setup(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace wk;
	workspace_init(&wk);

	OPTSTART("D:m:") {
	case 'D':
		if (!parse_config_opt(&wk, optarg)) {
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
		NULL)

	if (argi >= argc) {
		LOG_E("missing build dir");
		return false;
	}

	const char *build = argv[argi];
	++argi;

	if (!workspace_setup_dirs(&wk, build, argv[0])) {
		goto err;
	}

	uint32_t project_id;
	if (!eval_project(&wk, NULL, wk.source_root, wk.build_root, &project_id)) {
		goto err;
	}

	if (!output_build(&wk)) {
		goto err;
	}

	workspace_destroy(&wk);
	return true;
err:
	workspace_destroy(&wk);
	return false;
}

static bool
cmd_build(uint32_t argc, uint32_t argi, char *const argv[])
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
		NULL)

	return eval_internal(opts.cfg, argv[0]);
}

static bool
cmd_version(uint32_t argc, uint32_t argi, char *const argv[])
{
	printf("muon v%s-%s\nenabled features:",
		muon_version.version, muon_version.vcs_tag);
	if (have_curl) {
		printf(" curl");
	}

	if (have_libpkgconf) {
		printf(" libpkgconf");
	}

	if (have_zlib) {
		printf(" zlib");
	}

	if (have_samu) {
		printf(" samu");
	}

	printf("\n");
	return true;
}

static bool
cmd_main(uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "build", cmd_build, "build the project with default options" },
		{ "check", cmd_check, "check if a meson file parses" },
		{ "internal", cmd_internal, "internal subcommands" },
		{ "setup", cmd_setup, "setup a build directory" },
		{ "test", cmd_test, "run tests" },
		{ "version", cmd_version, "print version information" },
		{ "samu", cmd_samu, "run samurai" },
		{ 0 },
	};

	OPTSTART("vl") {
	case 'v':
		log_set_lvl(log_debug);
		break;
	case 'l':
		log_set_opts(log_show_source);
		break;
	} OPTEND(argv[0], "",
		"  -v - turn on debug messages"
		"  -l - show source locations for log messages",
		commands)

	cmd_func cmd;
	if (!find_cmd(commands, &cmd, argc, argi, argv, true)) {
		return false;
	}

	if (cmd) {
		return cmd(argc, argi, argv);
	} else {
		return cmd_build(argc, argi, argv);
	}

	return true;
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

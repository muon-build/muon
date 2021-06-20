#include "posix.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "external/curl.h"
#include "external/pkgconf.h"
#include "external/samu.h"
#include "external/zlib.h"
#include "filesystem.h"
#include "inih.h"
#include "log.h"
#include "mem.h"
#include "opts.h"
#include "output.h"
#include "parser.h"
#include "run_cmd.h"
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
		LOG_W(log_misc, "missing command");
		return false;
	}

	opts.cmd = &argv[argi];
	++argi;

	return true;

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
		LOG_W(log_misc, "missing filename");
		return NULL;
	}

	return argv[argi];
}

static bool
cmd_parse_check(uint32_t argc, uint32_t argi, char *const argv[])
{
	const char *filename;
	if (!(filename = get_filename_as_only_arg(argc, argi, argv))) {
		return false;
	}

	struct tokens toks = { 0 };
	struct ast ast = { 0 };
	if (!lexer_lex(language_internal, &toks, filename)) {
		return false;
	} else if (!parser_parse(&ast, &toks)) {
		return false;
	}

	return true;
}

static bool
cmd_ast(uint32_t argc, uint32_t argi, char *const argv[])
{
	const char *filename;
	if (!(filename = get_filename_as_only_arg(argc, argi, argv))) {
		return false;
	}

	struct tokens toks = { 0 };
	struct ast ast = { 0 };
	if (!lexer_lex(language_internal, &toks, filename)) {
		return false;
	} else if (!parser_parse(&ast, &toks)) {
		return false;
	}

	print_ast(&ast);
	return true;
}

static bool
cmd_eval(uint32_t argc, uint32_t argi, char *const argv[])
{
	const char *filename;
	if (!(filename = get_filename_as_only_arg(argc, argi, argv))) {
		return false;
	}

	char cwd[PATH_MAX + 1] = { 0 };
	if (!getcwd(cwd, PATH_MAX )) {
		LOG_W(log_misc, "failed getcwd: '%s'", strerror(errno));
		return false;
	}

	struct workspace wk;
	workspace_init(&wk);
	wk.lang_mode = language_internal;
	make_project(&wk, &wk.cur_project, NULL, cwd, "<build_dir>");
	bool ret;
	ret = eval(&wk, filename);

	workspace_destroy(&wk); // just to test for memory leaks in valgrind
	return ret;
}

static bool
cmd_internal(uint32_t argc, uint32_t argi, char *const argv[])
{
	static const struct command commands[] = {
		{ "exe", cmd_exe, "run an external command" },
		{ "ast", cmd_ast, "print a file's ast" },
		{ "eval", cmd_eval, "evaluate a file" },
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
		LOG_W(log_misc, "missing build dir");
		return false;
	}

	return tests_run(argv[argi]);
}

#define BUF_LEN 1024

static bool
setup_workspace_dirs(struct workspace *wk, const char *build, const char *argv0)
{
	static char cwd[BUF_LEN + 1] = { 0 },
		    abs_build[PATH_MAX + 1] = { 0 },
		    abs_argv0[PATH_MAX + 1] = { 0 };

	if (!getcwd(cwd, BUF_LEN)) {
		LOG_W(log_misc, "failed getcwd: '%s'", strerror(errno));
		return false;
	}

	snprintf(abs_build, PATH_MAX, "%s/%s", cwd, build);

	if (argv0[0] == '/') {
		strncpy(abs_argv0, argv0, PATH_MAX);
	} else {
		snprintf(abs_argv0, PATH_MAX, "%s/%s", cwd, argv0);
	}

	wk->argv0 = abs_argv0;
	wk->source_root = cwd;
	wk->build_root = abs_build;
	return true;
}

static bool
do_build(struct workspace *wk)
{
	uint32_t project_id;
	bool ret = false;
	char buf[PATH_MAX + 1] = { 0 };

	snprintf(buf, PATH_MAX, "%s/build.ninja", wk->build_root);

	if (!fs_file_exists(buf)) {
		if (!eval_project(wk, NULL, wk->source_root, wk->build_root, &project_id)) {
			goto ret;
		} else if (!output_build(wk)) {
			goto ret;
		}
	}

	if (have_samu) {
		if (chdir(wk->build_root) < 0) {
			goto ret;
		} else if (!muon_samu(0, (char *[]){ "<muon_samu>", NULL })) {
			goto ret;
		} else if (chdir(wk->source_root) < 0) {
			goto ret;
		}
	}

	ret = true;
ret:
	workspace_destroy(wk);
	return ret;
}

static bool
cmd_setup(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct workspace wk;
	workspace_init(&wk);

	OPTSTART("D:") {
	case 'D':
		if (!parse_config_opt(&wk, optarg)) {
			return false;
		}
		break;
	} OPTEND(argv[argi],
		" <build dir>",
		"  -D <option>=<value> - set project options\n",
		NULL)

	if (argi >= argc) {
		LOG_W(log_misc, "missing build dir");
		return false;
	}

	const char *build = argv[argi];
	++argi;

	if (!setup_workspace_dirs(&wk, build, argv[0])) {
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


struct build_cfg {
	const char *dir, *argv0;
	struct workspace wk;
	bool workspace_init;
};

static bool
build_cfg_cb(void *_cfg, const char *path, const char *sect,
	const char *k, const char *v, uint32_t line)
{
	struct build_cfg *cfg = _cfg;

	if (!sect) {
		error_messagef(path, line, 1, "missing [build_dir]");
		return false;
	}

	if (cfg->dir != sect) {
		if (cfg->workspace_init) {
			if (!do_build(&cfg->wk)) {
				return false;
			}
		}

		workspace_init(&cfg->wk);
		cfg->workspace_init = true;
		cfg->dir = sect;
		if (!setup_workspace_dirs(&cfg->wk, cfg->dir, cfg->argv0)) {
			return false;
		}
	}

	if (!parse_config_key_value(&cfg->wk, (char *)k, v)) {
		return false;
	}
	return true;
}

static bool
cmd_build(uint32_t argc, uint32_t argi, char *const argv[])
{
	struct {
		const char *cfg;
	} opts = { .cfg = ".muon" };

	OPTSTART("c:") {
	case 'c':
		opts.cfg = optarg;
		break;
	} OPTEND(argv[argi], "",
		"  -c config - load config alternate file (default: .muon)\n",
		NULL)

	struct build_cfg cfg = { .argv0 = argv[0] };
	bool ret = false;
	char *ini_buf = NULL;

	if (fs_file_exists(opts.cfg)) {
		if (!ini_parse(opts.cfg, &ini_buf, build_cfg_cb, &cfg)) {
			goto ret;
		}
	} else {
		workspace_init(&cfg.wk);
		cfg.workspace_init = true;

		if (!setup_workspace_dirs(&cfg.wk, "build", cfg.argv0)) {
			return false;
		}
	}

	if (cfg.workspace_init) {
		if (!do_build(&cfg.wk)) {
			goto ret;
		}
	}

	ret = true;
ret:
	if (ini_buf) {
		z_free(ini_buf);
	}
	return ret;
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
	log_init();
	log_set_lvl(log_debug);
	log_set_filters(0xffffffff & (~log_filter_to_bit(log_mem)));

	static const struct command commands[] = {
		{ "build", cmd_build, "build the project with default options" },
		{ "check", cmd_parse_check, "check if a meson file parses" },
		{ "internal", cmd_internal, "internal subcommands" },
		{ "setup", cmd_setup, "setup a build directory" },
		{ "test", cmd_test, "run tests" },
		{ "version", cmd_version, "print version information" },
		{ "samu", cmd_samu, "run samurai" },
		{ 0 },
	};

	OPTSTART("") {
	} OPTEND(argv[0], "", "", commands)

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
	return cmd_main(argc, 0, argv) ? 0 : 1;
}

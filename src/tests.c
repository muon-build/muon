#include "posix.h"

#include <limits.h>
#include <string.h>

#include "filesystem.h"
#include "log.h"
#include "mem.h"
#include "output.h"
#include "path.h"
#include "run_cmd.h"
#include "tests.h"

#define MAX_ARGS 64

struct test_parser {
	struct source src;
	const char *name;
	const char *cmd[MAX_ARGS];
	uint32_t i, argv;
	bool start, finished;
};

bool
next_test(struct test_parser *p)
{
	for (; p->i < p->src.len; ++p->i) {
		if (p->start) {
			p->name = &p->src.src[p->i];
			p->argv = 0;
			p->start = false;
		}

		if (!p->src.src[p->i]) {
			if (p->i + 1 >= p->src.len) {
				LOG_W(log_misc, "invalid test file");
				return false;
			}

			if (!p->src.src[p->i + 1]) {
				p->i += 2;
				p->start = true;

				if (p->i >= p->src.len) {
					p->finished = true;
				}

				return true;
			} else {
				p->cmd[p->argv] = &p->src.src[p->i + 1];
				++p->argv;
			}
		}
	}

	return false;
}

bool
tests_run(const char *build_root)
{
	bool ret = true;
	char tests_src[PATH_MAX], private[PATH_MAX];
	if (!path_join(private, PATH_MAX, build_root, outpath.private_dir)) {
		return false;
	} else if (!path_join(tests_src, PATH_MAX, private, outpath.tests)) {
		return false;
	}

	struct test_parser test_parser = { .start = true };
	if (!fs_read_entire_file(tests_src, &test_parser.src)) {
		return false;
	}

	if (chdir(build_root) != 0) {
		return false;
	}

	struct run_cmd_ctx cmd_ctx = { 0 };

	while (!test_parser.finished) {
		if (!next_test(&test_parser)) {
			return false;
		}

		LOG_I(log_misc, "%s - running", test_parser.name);

		if (!run_cmd(&cmd_ctx, test_parser.cmd[0], (char *const *)test_parser.cmd)) {
			if (cmd_ctx.err_msg) {
				LOG_W(log_misc, "error: %s", cmd_ctx.err_msg);
			} else {
				LOG_W(log_misc, "error: %s", strerror(cmd_ctx.err_no));
			}

			return false;
		}

		if (cmd_ctx.status) {
			LOG_W(log_misc, "%s - failed (%d)", test_parser.name, cmd_ctx.status);
			log_plain("%s", cmd_ctx.err);
			ret = false;
		} else {
			LOG_I(log_misc, "%s - success", test_parser.name);
		}
	}

	fs_source_destroy(&test_parser.src);
	return ret;
}

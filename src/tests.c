#include "posix.h"

#include <limits.h>
#include <string.h>

#include "filesystem.h"
#include "log.h"
#include "run_cmd.h"
#include "tests.h"

#define MAX_ARGS 64

struct test_parser {
	const char *name;
	char *cmd[MAX_ARGS];
	char *buf;
	uint64_t len;
	uint32_t i, argv;
	bool start, finished;
};

bool
next_test(struct test_parser *p)
{
	for (; p->i < p->len; ++p->i) {
		if (p->start) {
			p->name = &p->buf[p->i];
			p->argv = 0;
			p->start = false;
		}

		if (!p->buf[p->i]) {
			if (p->i + 1 >= p->len) {
				LOG_W(log_misc, "invalid test file");
				return false;
			}

			if (!p->buf[p->i + 1]) {
				p->i += 2;
				p->start = true;

				if (p->i >= p->len) {
					p->finished = true;
				}

				return true;
			} else {
				p->cmd[p->argv] = &p->buf[p->i + 1];
				++p->argv;
			}
		}
	}

	return false;
}

bool
tests_run(const char *build_root)
{
	char tests_src[PATH_MAX + 1] = { 0 };
	snprintf(tests_src, PATH_MAX, "%s/%s", build_root, "muon_tests.dat");

	struct test_parser test_parser = { .start = true };
	if (!fs_read_entire_file(tests_src, &test_parser.buf, &test_parser.len)) {
		return false;
	}


	struct run_cmd_ctx cmd_ctx = { 0 };

	while (!test_parser.finished) {
		if (!next_test(&test_parser)) {
			return false;
		}

		if (!run_cmd(&cmd_ctx, test_parser.cmd[0], test_parser.cmd)) {
			if (cmd_ctx.err_msg) {
				LOG_W(log_misc, "error: %s", cmd_ctx.err_msg);
			} else {
				LOG_W(log_misc, "error: %s", strerror(cmd_ctx.err_no));
			}

			return false;
		}

		if (cmd_ctx.status) {
			LOG_W(log_misc, "%s - failed (%d)", test_parser.name, cmd_ctx.status);
		} else {
			LOG_I(log_misc, "%s - succes", test_parser.name);
		}
	}

	return true;
}

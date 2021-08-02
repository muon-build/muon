#include "posix.h"

#include <limits.h>
#include <string.h>

#include "buf_size.h"
#include "log.h"
#include "output/output.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "tests.h"

struct test_parser {
	struct source src;
	const char *name;
	const char *cmd[MAX_ARGS];
	uint32_t test_flags;
	uint32_t i, argv;
	bool start, finished;
};

bool
next_test(struct test_parser *p)
{
	for (; p->i < p->src.len; ++p->i) {
		if (p->start) {
			if (p->i + 4 >= p->src.len) {
				LOG_E("invalid test file");
				return false;
			}

			memcpy(&p->test_flags, &p->src.src[p->i], sizeof(uint32_t));
			p->i += 4;

			p->name = &p->src.src[p->i];
			p->argv = 0;
			p->start = false;
		}

		if (!p->src.src[p->i]) {
			if (p->i + 1 >= p->src.len) {
				LOG_E("invalid test file");
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

	LOG_E("invalid test file");
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

		LOG_I("%s - running", test_parser.name);

		if (!run_cmd(&cmd_ctx, test_parser.cmd[0], (char *const *)test_parser.cmd)) {
			if (cmd_ctx.err_msg) {
				LOG_E("error: %s", cmd_ctx.err_msg);
			} else {
				LOG_E("error: %s", strerror(cmd_ctx.err_no));
			}

			return false;
		}

		if (cmd_ctx.status && !(test_parser.test_flags & test_flag_should_fail)) {
			LOG_E("%s - failed (%d)", test_parser.name, cmd_ctx.status);
			log_plain("%s", cmd_ctx.err);
			ret = false;
		} else {
			LOG_I("%s - success (%d)", test_parser.name, cmd_ctx.status);
		}
	}

	fs_source_destroy(&test_parser.src);
	return ret;
}

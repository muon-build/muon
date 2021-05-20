#include "posix.h"

#include "eval.h"
#include "filesystem.h"
#include "interpreter.h"
#include "log.h"
#include "mem.h"
#include "parser.h"

bool
eval_entry(struct workspace *wk, const char *src, const char *cwd, const char *build_dir)
{
	workspace_init(wk);
	struct project *proj = make_project(wk, &wk->cur_project);

	proj->cwd = wk_str_push(wk, cwd);
	proj->build_dir = wk_str_push(wk, build_dir);

	return eval(wk, src);
}

bool
eval(struct workspace *wk, const char *src)
{
	L(log_misc, "evaluating '%s'", src);

	struct ast ast = { 0 };
	if (!parse_file(&ast, src)) {
		return false;
	}

	if (!interpret(&ast, wk)) {
		return false;
	}

	return true;
}

void
error_message(const char *file, uint32_t line, uint32_t col, const char *fmt, va_list args)
{
	fprintf(stderr, "%s:%d:%d: \033[31merror:\033[0m ", file, line, col);
	vfprintf(stderr, fmt, args);
	putc('\n', stderr);

	char *buf;
	uint64_t len, i, cl = 1, sol = 0;
	if (fs_read_entire_file(file, &buf, &len)) {
		for (i = 0; i < len; ++i) {
			if (buf[i] == '\n') {
				++cl;
				sol = i + 1;
			}

			if (cl == line) {
				break;
			}
		}

		++i;
		for (; i < len; ++i) {
			if (buf[i] == '\n') {
				buf[i] = 0;
				break;
			}
		}

		fprintf(stderr, "%3d | %s\n", line, &buf[sol]);

		for (i = 1; i < col + 6; ++i) {
			putc(' ', stderr);
		}
		putc('^', stderr);
		putc('\n', stderr);

		z_free(buf);
	}
}

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

	struct tokens toks = { 0 };
	struct ast ast = { 0 }, *old_ast = wk->ast;

	if (!lexer_lex(&toks, src)) {
		goto err1;
	} else if (!parser_parse(&ast, &toks)) {
		goto err2;
	}

	wk->ast = &ast;
	if (!interpreter_interpret(wk)) {
		goto err3;
	}

	ast_destroy(&ast);
	tokens_destroy(&toks);
	wk->ast = old_ast;

	return true;

err3:
	wk->ast = old_ast;
	ast_destroy(&ast);
err2:
	tokens_destroy(&toks);
err1:
	return false;
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

		fprintf(stderr, "%3d | ", line);
		for (i = sol; buf[i] && buf[i] != '\n'; ++i) {
			if (buf[i] == '\t') {
				fputs("        ", stderr);
			} else {
				putc(buf[i], stderr);
			}
		}
		putc('\n', stderr);

		fputs("      ", stderr);
		for (i = 0; i < col; ++i) {
			if (buf[sol + i] == '\t') {
				fputs("        ", stderr);
			} else {
				putc(i == col - 1 ? '^' : ' ', stderr);
			}
		}
		putc('\n', stderr);

		z_free(buf);
	}
}

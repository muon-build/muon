#include "posix.h"

#include <limits.h>
#include <string.h>

#include "eval.h"
#include "filesystem.h"
#include "functions/default/options.h"
#include "interpreter.h"
#include "log.h"
#include "mem.h"
#include "parser.h"
#include "wrap.h"

bool
eval_project(struct workspace *wk, const char *subproject_name,
	const char *cwd, const char *build_dir, uint32_t *proj_id)
{
	char src[PATH_MAX + 1] = { 0 }, meson_opts[PATH_MAX + 1] = { 0 };

	snprintf(src, PATH_MAX, "%s/%s", cwd, "meson.build");
	snprintf(meson_opts, PATH_MAX, "%s/%s", cwd, "meson_options.txt");

	if (!fs_dir_exists(cwd)) {
		int32_t i;
		char wrap[PATH_MAX + 1] = { 0 }, basedir[PATH_MAX + 1] = { 0 };

		strncpy(basedir, cwd, PATH_MAX);
		snprintf(wrap, PATH_MAX, "%s.wrap", cwd);

		for (i = strlen(cwd); i >= 0; --i) {
			if (basedir[i] == '/') {
				basedir[i] = 0;
				break;
			}
		}

		if (fs_file_exists(wrap)) {
			if (!wrap_handle(wk, wrap, basedir)) {
				return false;
			}
		} else {
			LOG_W(log_misc, "project %s not found", cwd);
			return false;
		}
	}

	if (!fs_file_exists(src)) {
		LOG_W(log_misc, "project %s does not contain a meson.build", cwd);
		return false;
	}

	bool ret = false;
	uint32_t parent_project = wk->cur_project;

	make_project(wk, &wk->cur_project, subproject_name, cwd, build_dir);
	*proj_id = wk->cur_project;

	set_default_options(wk);

	if (fs_file_exists(meson_opts)) {
		wk->lang_mode = language_opts;
		if (!eval(wk, meson_opts)) {
			goto cleanup;
		}
	}

	wk->lang_mode = language_external;

	ret = eval(wk, src) && check_unused_option_overrides(wk);

cleanup:
	wk->cur_project = parent_project;
	return ret;
}

bool
eval(struct workspace *wk, const char *src)
{
	/* L(log_misc, "evaluating '%s'", src); */

	/* TODO:
	 *
	 * currently ids and string literals are stored as pointers to
	 * locations in the block originally read to do lexing, which is owned
	 * by struct tokens.  Because of this, tokens need to stay live until
	 * the parent project is done evaluating.  This should be fixed by
	 * storing these strings in the workspace's string buffer.
	 */

	uint32_t parent_tokens = current_project(wk)->cur_tokens;

	current_project(wk)->cur_tokens = darr_push(&current_project(wk)->tokens, &(struct tokens) { 0 });
	struct tokens *toks = darr_get(&current_project(wk)->tokens, current_project(wk)->cur_tokens);
	struct ast ast = { 0 };
	bool ret;

	if (!lexer_lex(wk->lang_mode, toks, src)) {
		return false;
	} else if (!parser_parse(&ast, toks)) {
		/* tokens_destroy(&toks); // See above note */
		return false;
	}

	struct ast *parent_ast = wk->ast;
	wk->ast = &ast;

	ret = interpreter_interpret(wk);
	/* L(log_misc, "done evaluating '%s'", src); */

	current_project(wk)->cur_tokens = parent_tokens;
	wk->ast = parent_ast;
	if (parent_ast) {
		wk->ast->toks = darr_get(&current_project(wk)->tokens, current_project(wk)->cur_tokens);
	}

	/* tokens_destroy(&toks); // See above note */
	ast_destroy(&ast);

	return ret;
}

void
error_message(const char *file, uint32_t line, uint32_t col, const char *fmt, va_list args)
{
	const char *label = log_clr() ? "\033[31merror:\033[0m" : "error:";

	log_plain("%s:%d:%d: %s ", file, line, col, label);
	log_plainv(fmt, args);
	log_plain("\n");

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

		log_plain("%3d | ", line);
		for (i = sol; buf[i] && buf[i] != '\n'; ++i) {
			if (buf[i] == '\t') {
				log_plain("        ");
			} else {
				putc(buf[i], stderr);
			}
		}
		log_plain("\n");

		log_plain("      ");
		for (i = 0; i < col; ++i) {
			if (buf[sol + i] == '\t') {
				log_plain("        ");
			} else {
				log_plain(i == col - 1 ? "^" : " ");
			}
		}
		log_plain("\n");

		z_free(buf);
	}
}

void
error_messagef(const char *file, uint32_t line, uint32_t col, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	error_message(file, line, col, fmt, ap);
	va_end(ap);
}

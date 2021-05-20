#include "posix.h"

#include "eval.h"
#include "interpreter.h"
#include "log.h"
#include "parse.h"

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
print_ast(const char *src)
{
	struct ast ast = { 0 };
	if (!parse_file(&ast, src)) {
		return;
	}

	uint32_t i;
	for (i = 0; i < ast.ast.len; ++i) {
		print_tree(&ast, *(uint32_t *)darr_get(&ast.ast, i), 0);
	}
}

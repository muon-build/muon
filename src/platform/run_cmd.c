/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifdef __sun
/* for signals */
#define __EXTENSIONS__
#endif

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/run_cmd.h"

void
push_argv_single(const char **argv, uint32_t *len, uint32_t max, const char *arg)
{
	assert(*len < max && "too many arguments");
	argv[*len] = arg;
	++(*len);
}

void
argstr_pushall(const char *argstr, uint32_t argc, const char **argv, uint32_t *argi, uint32_t max)
{
	if (!argc) {
		return;
	}

	const char *p, *arg;
	uint32_t i = 0;

	arg = p = argstr;
	for (;; ++p) {
		if (!p[0]) {
			push_argv_single(argv, argi, max, arg);

			if (++i >= argc) {
				break;
			}

			arg = p + 1;
		}
	}
}

uint32_t
argstr_to_argv(const char *argstr, uint32_t argc, const char *prepend, char *const **res)
{
	uint32_t argi = 0, max = argc;

	if (prepend) {
		max += 1;
	}

	const char **new_argv = z_calloc(max + 1, sizeof(const char *));

	if (prepend) {
		push_argv_single(new_argv, &argi, max, prepend);
	}

	argstr_pushall(argstr, argc, new_argv, &argi, max);

	*res = (char *const *)new_argv;
	return argi;
}

static bool
run_cmd_determine_interpreter_skip_whitespace(char **p, bool invert)
{
	bool char_found;

	while (**p) {
		char_found = is_whitespace_except_newline(**p);

		if (invert) {
			if (char_found) {
				return true;
			}
		} else {
			if (!char_found) {
				return true;
			}
		}

		++*p;
	}

	return false;
}

bool
run_cmd_determine_interpreter(struct source *src,
	const char *path,
	const char **err_msg,
	const char **new_argv0,
	const char **new_argv1)
{
	if (!fs_read_entire_file(path, src)) {
		*err_msg = "error determining command interpreter: failed to read file";
		return false;
	}

	if (strncmp(src->src, "#!", 2) != 0) {
		*err_msg = "error determining command interpreter: missing #!";
		return false;
	}

	char *p, *q;
	p = (char *)&src->src[2];

	for (q = p; *q; ++q) {
		if (*q == '\n' || *q == '\r') {
			*q = 0;
			break;
		}
	}

	// skip over all whitespace characters before the next token
	if (!run_cmd_determine_interpreter_skip_whitespace(&p, false)) {
		*err_msg = "error determining command interpreter: no interpreter specified after #!";
		return false;
	}

	*new_argv0 = p;
	*new_argv1 = 0;

	// skip over all non-whitespace characters
	if (!run_cmd_determine_interpreter_skip_whitespace(&p, true)) {
		return true;
	}

	*p = 0;
	++p;

	// skip over all whitespace characters before the next token
	if (!run_cmd_determine_interpreter_skip_whitespace(&p, false)) {
		return true;
	}

	*new_argv1 = p;

	return true;
}

void
run_cmd_print_error(struct run_cmd_ctx *ctx, enum log_level lvl)
{
	if (ctx->err_msg) {
		log_print(true, lvl, "%s", ctx->err_msg);
	}

	if (ctx->out.len) {
		log_print(false, lvl, "stdout:\n%s", ctx->out.buf);
	}

	if (ctx->err.len) {
		log_print(false, lvl, "stderr:\n%s", ctx->err.buf);
	}
}

bool
run_cmd_checked(struct run_cmd_ctx *ctx, const char *argstr, uint32_t argc, const char *envstr, uint32_t envc)
{
	if (!run_cmd(ctx, argstr, argc, envstr, envc) || ctx->status != 0) {
		run_cmd_print_error(ctx, log_error);
		run_cmd_ctx_destroy(ctx);
		return false;
	}

	return true;
}

bool
run_cmd_argv_checked(struct run_cmd_ctx *ctx, char *const *argv, const char *envstr, uint32_t envc)
{
	if (!run_cmd_argv(ctx, argv, envstr, envc) || ctx->status != 0) {
		run_cmd_print_error(ctx, log_error);
		run_cmd_ctx_destroy(ctx);
		return false;
	}

	return true;
}

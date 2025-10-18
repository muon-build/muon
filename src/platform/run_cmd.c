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

#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
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
argstr_to_argv(struct workspace *wk, const char *argstr, uint32_t argc, const char *prepend, char *const **res)
{
	uint32_t argi = 0, max = argc;

	if (prepend) {
		max += 1;
	}

	const char **new_argv = ar_maken(wk->a_scratch, const char *, max + 1);

	if (prepend) {
		push_argv_single(new_argv, &argi, max, prepend);
	}

	argstr_pushall(argstr, argc, new_argv, &argi, max);

	*res = (char *const *)new_argv;
	return argi;
}

enum run_cmd_determine_interpreter_skip_mode {
	run_cmd_determine_interpreter_skip_mode_whitespace,
	run_cmd_determine_interpreter_skip_mode_non_whitespace,
};

static bool
run_cmd_determine_interpreter_skip_chars(char **p, enum run_cmd_determine_interpreter_skip_mode mode)
{
	bool is_whitespace;

	while (**p) {
		is_whitespace = is_whitespace_except_newline(**p);

		switch (mode) {
		case run_cmd_determine_interpreter_skip_mode_whitespace:
			if (!is_whitespace) {
				return true;
			}
			break;
		case run_cmd_determine_interpreter_skip_mode_non_whitespace:
			if (is_whitespace) {
				return true;
			}
			break;
		}

		++*p;
	}

	return false;
}

static bool
run_cmd_determine_interpreter_from_file(struct workspace *wk,
	const char *path,
	const char **err_msg,
	const char **new_argv0,
	const char **new_argv1)
{
	uint64_t buf_size = 2048;

	FILE *f;
	char *buf = ar_alloc(wk->a_scratch, buf_size, 1, 1);
	if (!(f = fs_fopen(path, "rb"))) {
		*err_msg = "error determining command interpreter: failed to read file";
		return false;
	}

	fread(buf, 1, buf_size - 1, f);

	if (!fs_fclose(f)) {
		*err_msg = "error determining command interpreter: failed to close file";
		return false;
	}

	if (strncmp(buf, "#!", 2) != 0) {
		*err_msg = "error determining command interpreter: missing #!";
		return false;
	}

	char *p, *q;
	p = (char *)&buf[2];

	bool found_line_end = false;
	for (q = p; *q; ++q) {
		if (*q == '\n' || *q == '\r') {
			*q = 0;
			found_line_end = true;
			break;
		}
	}

	if (!found_line_end) {
		*err_msg = "error determining command interpreter: #! line too long";
		return false;
	}

	// skip over all whitespace characters before the next token
	if (!run_cmd_determine_interpreter_skip_chars(&p, run_cmd_determine_interpreter_skip_mode_whitespace)) {
		*err_msg = "error determining command interpreter: no interpreter specified after #!";
		return false;
	}

	*new_argv0 = p;
	*new_argv1 = 0;

	// skip over all non-whitespace characters
	if (!run_cmd_determine_interpreter_skip_chars(&p, run_cmd_determine_interpreter_skip_mode_non_whitespace)) {
		return true;
	}

	*p = 0;
	++p;

	// skip over all whitespace characters before the next token
	if (!run_cmd_determine_interpreter_skip_chars(&p, run_cmd_determine_interpreter_skip_mode_whitespace)) {
		return true;
	}

	*new_argv1 = p;

	return true;
}

bool
run_cmd_determine_interpreter(struct workspace *wk,
	const char *path,
	const char **err_msg,
	const char **new_argv0,
	const char **new_argv1)
{
	if (host_machine.is_windows) {
		if (fs_has_extension(path, ".bat")) {
			*new_argv0 = "cmd.exe";
			*new_argv1 = "/c";
			return true;
		}
	}

	if (!run_cmd_determine_interpreter_from_file(wk, path, err_msg, new_argv0, new_argv1)) {
		return false;
	}

	// skip /usr/bin/env on windows
	if (host_machine.is_windows && *new_argv1 && strcmp(*new_argv0, "/usr/bin/env") == 0) {
		*new_argv0 = *new_argv1;
		*new_argv1 = 0;
	}

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
run_cmd_checked(struct workspace *wk,
	struct run_cmd_ctx *ctx,
	const char *argstr,
	uint32_t argc,
	const char *envstr,
	uint32_t envc)
{
	if (!run_cmd(wk, ctx, argstr, argc, envstr, envc) || ctx->status != 0) {
		run_cmd_print_error(ctx, log_error);
		run_cmd_ctx_destroy(ctx);
		return false;
	}

	return true;
}

bool
run_cmd_argv_checked(struct workspace *wk, struct run_cmd_ctx *ctx, char *const *argv, const char *envstr, uint32_t envc)
{
	if (!run_cmd_argv(wk, ctx, argv, envstr, envc) || ctx->status != 0) {
		run_cmd_print_error(ctx, log_error);
		run_cmd_ctx_destroy(ctx);
		return false;
	}

	return true;
}

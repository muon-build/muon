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

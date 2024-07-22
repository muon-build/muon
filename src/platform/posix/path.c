/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "lang/string.h"
#include "platform/path.h"

bool
path_is_absolute(const char *path)
{
	return *path == PATH_SEP;
}

bool
path_is_basename(const char *path)
{
	return strchr(path, PATH_SEP) == NULL;
}

void
path_to_posix(char *path)
{
	(void)path;
}

void
shell_escape(struct workspace *wk, struct sbuf *sb, const char *str)
{
	const char *need_escaping = "\"'$ \\><&#()\n";
	const char *s;
	bool do_esc = false;

	if (!*str) {
		sbuf_pushs(wk, sb, "''");
		return;
	}

	for (s = str; *s; ++s) {
		if (strchr(need_escaping, *s)) {
			do_esc = true;
			break;
		}
	}

	if (!do_esc) {
		sbuf_pushs(wk, sb, str);
		return;
	}

	sbuf_push(wk, sb, '\'');

	for (s = str; *s; ++s) {
		if (*s == '\'') {
			sbuf_pushs(wk, sb, "'\\''");
		} else {
			sbuf_push(wk, sb, *s);
		}
	}

	sbuf_push(wk, sb, '\'');
}

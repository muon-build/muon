/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "guess.h"
#include "log.h"
#include "platform/assert.h"

bool
guess_version(struct workspace *wk, const char *src, obj *res)
{
	uint32_t dots = 0, ver_len = 0, new_len, new_dots;
	const char *p, *ver = NULL;

	if (!src) {
		return false;
	}

	for (p = src; *p; ++p) {
		new_len = 0;
		new_dots = 0;
		while (('0' <= p[new_len] && p[new_len] <= '9') || p[new_len] == '.') {
			if (p[new_len] == '.') {
				++new_dots;
			}
			++new_len;
		}

		if (new_dots > dots) {
			ver = p;
			dots = new_dots;
			ver_len = new_len;
		}

		if (new_len) {
			p += new_len - 1;
		}
	}

	if (!ver) {
		return false;
	}

	obj s = make_strn(wk, ver, ver_len);
	*res = s;
	return true;
}

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "formats/lines.h"
#include "formats/tap.h"
#include "lang/string.h"

struct tap_parse_ctx {
	struct tap_parse_result *res;
	bool have_plan;
	bool bail_out;
};

static enum iteration_result
tap_parse_line_cb(void *_ctx, const char *line, size_t len)
{
	struct tap_parse_ctx *ctx = _ctx;
	struct str l = { .s = line, .len = len }, rest;
	bool ok;

	if (str_startswith(&l, &WKSTR("1..")) && l.len > 3) {
		struct str i = { .s = &l.s[3], l.len - 3 };
		int64_t plan_count;
		if (str_to_i(&i, &plan_count, false) && plan_count > 0) {
			ctx->have_plan = true;
			ctx->res->total = plan_count;
		}

		return ir_cont;
	} else if (str_startswith(&l, &WKSTR("Bail out!"))) {
		ctx->bail_out = true;
		return ir_cont;
	} else if (str_startswith(&l, &WKSTR("ok"))) {
		ok = true;
		rest = (struct str){ .s = &l.s[2], .len = l.len - 2 };
	} else if (str_startswith(&l, &WKSTR("not ok"))) {
		ok = false;
		rest = (struct str){ .s = &l.s[6], .len = l.len - 6 };
	} else {
		return ir_cont;
	}

	enum {
		none,
		todo,
		skip,
	} directive
		= none;
	{
		char *directive_str;
		if ((directive_str = strstr(rest.s, " # "))) {
			directive_str += 3;
			if (str_startswithi(&WKSTR(directive_str), &WKSTR("todo"))) {
				directive = todo;
			} else if (str_startswithi(&WKSTR(directive_str), &WKSTR("skip"))) {
				directive = skip;
			}
		}
	}

	if (directive == skip) {
		++ctx->res->skip;
		return ir_cont;
	}

	if (ok) {
		++ctx->res->pass;
	} else {
		if (directive == todo) {
			++ctx->res->skip;
		} else {
			++ctx->res->fail;
		}
	}

	return ir_cont;
}

void
tap_parse(const char *buf, uint64_t buf_len, struct tap_parse_result *res)
{
	struct tap_parse_ctx ctx = { .res = res };

	each_line_const(buf, buf_len, &ctx, tap_parse_line_cb);

	res->have_plan = ctx.have_plan;

	if (!ctx.have_plan) {
		res->total = res->pass + res->skip + res->fail;
	}

	res->all_ok = res->total == res->pass + res->skip;
}

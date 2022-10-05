#include "posix.h"

#include <string.h>
#include <strings.h>

#include "formats/lines.h"
#include "formats/tap.h"
#include "lang/string.h"
#include "log.h"

struct tap_parse_ctx {
	struct tap_parse_result *res;
	bool have_plan;
	bool bail_out;
};

static enum iteration_result
tap_parse_line_cb(void *_ctx, char *line, size_t _)
{
	struct tap_parse_ctx *ctx = _ctx;
	struct str l = WKSTR(line), rest;
	bool ok;

	if (str_startswith(&l, &WKSTR("1..")) && l.len > 3) {
		struct str i = { .s = &l.s[3], l.len - 3 };
		int64_t plan_count;
		if (str_to_i(&i, &plan_count) && plan_count > 0) {
			ctx->have_plan = true;
			ctx->res->total = plan_count;
		}

		return ir_cont;
	} else if (str_startswith(&l, &WKSTR("Bail out!"))) {
		ctx->bail_out = true;
		return ir_cont;
	} else if (str_startswith(&l, &WKSTR("ok"))) {
		ok = true;
		rest = (struct str) { .s = &l.s[2], .len = l.len - 2 };
	} else if (str_startswith(&l, &WKSTR("not ok"))) {
		ok = false;
		rest = (struct str) { .s = &l.s[6], .len = l.len - 6 };
	} else {
		return ir_cont;
	}

	enum { none, todo, skip, } directive = none;
	{
		char *directive_str;
		if ((directive_str = strstr(rest.s, " # "))) {
			directive_str += 3;
			if (strncasecmp(directive_str, "todo", 4) == 0) {
				directive = todo;
			} else if (strncasecmp(directive_str, "skip", 4) == 0) {
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
tap_parse(char *buf, uint64_t buf_len, struct tap_parse_result *res)
{
	struct tap_parse_ctx ctx = { .res = res };

	each_line(buf, buf_len, &ctx, tap_parse_line_cb);

	if (!ctx.have_plan) {
		res->total = res->pass + res->skip + res->fail;
	}

	res->all_ok = res->total == res->pass + res->skip;
}

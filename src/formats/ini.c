/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "formats/ini.h"
#include "formats/lines.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "platform/filesystem.h"

struct ini_parse_ctx {
	struct workspace *wk;
	struct source src;
	struct source_location location;
	const char *comment_chars;
	bool keyval;
	void *octx;
	char *sect;
	inihcb cb;
	bool success;
};

static bool
line_is_whitespace(const char *c)
{
	for (; *c; ++c) {
		if (!is_whitespace(*c)) {
			return false;
		}
	}

	return true;
}

static enum iteration_result
ini_parse_line_cb(void *_ctx, char *line, size_t len)
{
	struct ini_parse_ctx *ctx = _ctx;
	char *ptr, *key, *val;

	if (!*line || strchr(ctx->comment_chars, *line) || line_is_whitespace(line)) {
		goto done_with_line;
	} else if (!ctx->keyval && *line == '[') {
		if (!(ptr = strchr(line, ']'))) {
			error_messagef(ctx->wk, &ctx->src, ctx->location, log_error, "expected matching ']'");
			ctx->success = false;
			goto done_with_line;
		}

		*ptr = '\0';

		ctx->sect = line + 1;

		if (!ctx->cb(ctx->octx, &ctx->src, ctx->sect, NULL, NULL, ctx->location)) {
			ctx->success = false;
		}
		goto done_with_line;
	}

	if (!(ptr = strchr(line, '='))) {
		if (!ctx->keyval) {
			ctx->location.len = len;
			error_messagef(ctx->wk, &ctx->src, ctx->location, log_error, "expected '=' in line");
			ctx->success = false;
		}
		goto done_with_line;
	}

	*ptr = '\0';

	key = line;
	val = ptr - 1;
	while (is_whitespace(*val)) {
		*val = '\0';
		--val;
	}

	val = ptr + 1;
	while (is_whitespace(*val)) {
		++val;
	}

	char *val_end = val + strlen(val) - 1;
	while (is_whitespace(*val_end)) {
		*val_end = '\0';
		--val_end;
	}

	if (!ctx->cb(ctx->octx, &ctx->src, ctx->sect, key, val, ctx->location)) {
		ctx->success = false;
	}

done_with_line:
	if (!ctx->success) {
		return ir_done;
	}

	ctx->location.off += len + 1;

	return ir_cont;
}

bool
ini_reparse(struct workspace *wk, const char *path, const struct source *src, char *buf, inihcb cb, void *octx)
{
	struct ini_parse_ctx ctx = {
		.wk = wk,
		.comment_chars = ";#",
		.octx = octx,
		.cb = cb,
		.location = { 0, 1 },
		.success = true,
		.src = *src,
	};

	memcpy(buf, ctx.src.src, ctx.src.len);

	each_line(buf, ctx.src.len, &ctx, ini_parse_line_cb);
	return ctx.success;
}

bool
ini_parse(struct workspace *wk, const char *path, struct source *src, char **buf, inihcb cb, void *octx)
{
	if (!fs_read_entire_file(wk->a_scratch, path, src)) {
		return false;
	}

	*buf = ar_alloc(wk->a_scratch, 1, src->len + 1, 1);

	return ini_reparse(wk, path, src, *buf, cb, octx);
}

bool
keyval_parse(struct workspace *wk, const char *path, struct source *src, char **buf, inihcb cb, void *octx)
{
	if (!fs_read_entire_file(wk->a_scratch, path, src)) {
		return false;
	}

	*buf = ar_alloc(wk->a_scratch, 1, src->len + 1, 1);

	struct ini_parse_ctx ctx = {
		.wk = wk,
		.comment_chars = "#",
		.keyval = true,
		.octx = octx,
		.cb = cb,
		.location = { 0, 1 },
		.success = true,
		.src = *src,
	};

	memcpy(*buf, ctx.src.src, ctx.src.len);

	each_line(*buf, ctx.src.len, &ctx, ini_parse_line_cb);
	return ctx.success;
}

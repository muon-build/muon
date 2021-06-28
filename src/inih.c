#include "posix.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "filesystem.h"
#include "inih.h"
#include "iterator.h"
#include "log.h"
#include "mem.h"

typedef enum iteration_result ((*each_line_callback)(void *ctx, char *line, size_t len));

static void
each_line(char *buf, uint64_t len, void *ctx, each_line_callback cb)
{
	char *line, *b;

	line = buf;

	while ((b = strchr(line, '\n'))) {
		*b = '\0';

		if (cb(ctx, line, b - line) != ir_cont) {
			break;
		}

		line = b + 1;

		if ((size_t)(line - buf) >= len) {
			break;
		}
	}
}

struct each_line_ctx {
	struct source src;
	void *octx;
	char *sect;
	inihcb cb;
	uint32_t line;
	bool success;
};

static bool
is_whitespace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static enum iteration_result
each_line_cb(void *_ctx, char *line, size_t len)
{
	struct each_line_ctx *ctx = _ctx;
	char *ptr, *key, *val;

	if (*line == '\0' || *line == ';' || *line == '#') {
		goto done_with_line;
	} else if (*line == '[') {
		if (!(ptr = strchr(line, ']'))) {
			error_messagef(&ctx->src, ctx->line, strlen(line) + 1, "expected ']'");
			ctx->success = false;
			goto done_with_line;
		}

		*ptr = '\0';

		ctx->sect = line + 1;

		if (!ctx->cb(ctx->octx, &ctx->src, ctx->sect, NULL, NULL, ctx->line)) {
			ctx->success = false;
		}
		goto done_with_line;
	}

	if (!(ptr = strchr(line, '='))) {
		error_messagef(&ctx->src, ctx->line, strlen(line) + 1, "expected '='");
		ctx->success = false;
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

	if (!ctx->cb(ctx->octx, &ctx->src, ctx->sect, key, val, ctx->line)) {
		ctx->success = false;
	}

done_with_line:
	if (!ctx->success) {
		return ir_done;
	}

	++ctx->line;

	return ir_cont;
}

bool
ini_parse(const char *path, char **buf, inihcb cb, void *octx)
{
	*buf = NULL;
	bool ret = false;

	struct each_line_ctx ctx = {
		.octx = octx,
		.cb = cb,
		.line = 1,
		.success = true,
	};

	uint64_t len;
	if (!fs_read_entire_file(path, &ctx.src)) {
		goto ret;
	}

	len = ctx.src.len;
	*buf = z_malloc(len);
	memcpy(*buf, ctx.src.src, len);

	each_line(*buf, len, &ctx, each_line_cb);

	ret = true;
ret:
	fs_source_destroy(&ctx.src);
	return ret && ctx.success;
}

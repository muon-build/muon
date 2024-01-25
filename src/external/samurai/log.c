/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "external/samurai/ctx.h"
#include "formats/lines.h"
#include "log.h"

#include "external/samurai/graph.h"
#include "external/samurai/log.h"
#include "external/samurai/util.h"

static const char *samu_logname = ".ninja_log";
static const char *samu_log_version_fmt = "# ninja log v%d\n";
static const int samu_logver = 5;

enum samu_log_field {
	samu_log_field_start_time,
	samu_log_field_end_time,
	samu_log_field_mtime,
	samu_log_field_output_path,
	samu_log_field_command_hash,
	samu_log_field_count,
};

struct samu_log_parse_ctx {
	uint32_t line_no;
	size_t nentry;
	struct samu_ctx *samu_ctx;
};

static enum iteration_result
samu_log_parse_cb(void *_ctx, char *line, size_t len)
{
	struct samu_log_parse_ctx *ctx = _ctx;
	char *fields[samu_log_field_count] = { 0 };
	struct samu_node *n;

	if (ctx->line_no == 1) {
		int ver;
		if (sscanf(line, samu_log_version_fmt, &ver) < 1 || ver != samu_logver) {
			return ir_done;
		}

		goto cont;
	}

	{
		char *p = line;
		uint32_t i;
		for (i = 0; i < samu_log_field_count; ++i) {
			fields[i] = p;
			if (!(p = strchr(p, '\t'))) {
				break;
			}

			*p = 0;
			++p;
		}
	}

	{ // get node
		if (!fields[samu_log_field_output_path]) {
			samu_warn("missing output path");
			goto corrupt_line;
		}

		n = samu_nodeget(ctx->samu_ctx, fields[samu_log_field_output_path], 0);
		if (!n || !n->gen) {
			goto cont;
		}
		if (n->logmtime == SAMU_MTIME_MISSING) {
			++ctx->nentry;
		}
	}

	{ // get mtime
		if (!fields[samu_log_field_mtime]) {
			samu_warn("missing mtime");
			goto corrupt_line;
		}

		char *endptr;
		n->logmtime = strtoll(fields[samu_log_field_mtime], &endptr, 10);
		if (*endptr) {
			samu_warn("invalid mtime: %s", fields[samu_log_field_mtime]);
			goto corrupt_line;
		}
	}

	{ // get output hash
		if (!fields[samu_log_field_command_hash]) {
			samu_warn("missing command hash");
			goto corrupt_line;
		}

		char *endptr;
		n->hash = strtoull(fields[samu_log_field_command_hash], &endptr, 16);
		if (*endptr) {
			samu_warn("invalid hash for '%s'", n->path->s);
			goto corrupt_line;
		}
	}

cont:
	++ctx->line_no;
	return ir_cont;
corrupt_line:
	samu_warn("corrupt build log @ line %d", ctx->line_no);
	goto cont;
}

static void
samu_log_open_and_write(struct samu_ctx *ctx, const char *builddir, bool write_graph)
{
	const struct samu_edge *e;
	struct samu_node *n;
	uint32_t i;

	const char *logpath;
	if (builddir) {
		char *path;
		samu_xasprintf(&ctx->arena, &path, "%s/%s", builddir, samu_logname);
		logpath = path;
	} else {
		logpath = samu_logname;
	}

	if (!(ctx->log.logfile = fs_fopen(logpath, "wb"))) {
		samu_fatal("open %s", logpath);
		return;
	}

	fprintf(ctx->log.logfile, samu_log_version_fmt, samu_logver);

	if (write_graph) {
		for (e = ctx->graph.alledges; e; e = e->allnext) {
			for (i = 0; i < e->nout; ++i) {
				n = e->out[i];
				if (!n->hash) {
					continue;
				}
				samu_logrecord(ctx, n);
			}
		}
	}
}

void
samu_loginit(struct samu_ctx *ctx, const char *builddir)
{
	char *logpath = (char *)samu_logname;

	if (ctx->log.logfile) {
		samu_logclose(ctx);
	}

	if (builddir) {
		samu_xasprintf(&ctx->arena, &logpath, "%s/%s", builddir, samu_logname);
	}

	if (!fs_exists(logpath)) {
		samu_log_open_and_write(ctx, builddir, false);
		return;
	}

	struct source src = { 0 };
	if (!fs_read_entire_file(logpath, &src)) {
		samu_fatal("failed to read log file at %s", logpath);
	}

	struct samu_log_parse_ctx samu_log_parse_ctx = {
		.line_no = 1,
		.samu_ctx = ctx,
	};

	each_line((char *)src.src, src.len, &samu_log_parse_ctx, samu_log_parse_cb);

	fs_source_destroy(&src);

	/* if (samu_log_parse_ctx.line_no <= 100 || samu_log_parse_ctx.line_no <= 3 * samu_log_parse_ctx.nentry) { */
	/* 	return; */
	/* } */

	samu_log_open_and_write(ctx, builddir, true);
}

void
samu_logclose(struct samu_ctx *ctx)
{
	fs_fclose(ctx->log.logfile);
	ctx->log.logfile = NULL;
}

void
samu_logrecord(struct samu_ctx *ctx, struct samu_node *n)
{
	fprintf(ctx->log.logfile, "0\t0\t%" PRId64 "\t%s\t%" PRIx64 "\n", n->logmtime, n->path->s, n->hash);
}

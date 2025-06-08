/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "buf_size.h"
#include "external/samurai/ctx.h"
#include "platform/assert.h"
#include "platform/os.h"
#include "platform/path.h"

#include "external/samurai.h"
#include "external/samurai/arg.h"
#include "external/samurai/build.h"
#include "external/samurai/deps.h"
#include "external/samurai/env.h"
#include "external/samurai/graph.h"
#include "external/samurai/log.h"
#include "external/samurai/parse.h"
#include "external/samurai/tool.h"
#include "external/samurai/util.h"

static void
samu_usage(struct samu_ctx *ctx)
{
	fprintf(stderr, "usage: %s [-C dir] [-d debugflag] [-f buildfile] [-j maxjobs]"
		" [-k maxfail] [-n] [-t tool] [-v] [-w warnflag] [target...]\n"
		"       %s -h | --help\n",
		ctx->argv0, ctx->argv0);
	exit(2);
}

static char *
samu_getbuilddir(struct samu_ctx *ctx)
{
	struct samu_string *builddir;

	builddir = samu_envvar(ctx->env.rootenv, "builddir");
	if (!builddir)
		return NULL;
	if (samu_makedirs(builddir, false) < 0)
		exit(1);
	return builddir->s;
}

static void
samu_debugflag(struct samu_ctx *ctx, const char *flag)
{
	if (strcmp(flag, "explain") == 0)
		ctx->buildopts.explain = true;
	else if (strcmp(flag, "keepdepfile") == 0)
		ctx->buildopts.keepdepfile = true;
	else if (strcmp(flag, "keeprsp") == 0)
		ctx->buildopts.keeprsp = true;
	else
		samu_fatal("unknown debug flag '%s'", flag);
}

static void
samu_warnflag(struct samu_ctx *ctx, const char *flag)
{
	if (strcmp(flag, "dupbuild=err") == 0)
		ctx->parseopts.dupbuildwarn = false;
	else if (strcmp(flag, "dupbuild=warn") == 0)
		ctx->parseopts.dupbuildwarn = true;
	else
		samu_fatal("unknown warning flag '%s'", flag);
}

static void
samu_jobsflag(struct samu_ctx *ctx, const char *flag)
{
	long num;
	char *end;

	num = strtol(flag, &end, 10);
	if (*end || num <= 0)
		samu_fatal("invalid -j parameter");
	ctx->buildopts.maxjobs = num;
}

static void
samu_parseenvargs(struct samu_ctx *ctx, const char *_env)
{
	char *arg, *argvbuf[64], **argv = argvbuf;
	int argc;

	if (!_env)
		return;
	char *env = samu_xmemdup(&ctx->arena, _env, strlen(_env) + 1);
	argc = 1;
	argv[0] = NULL;
	arg = strtok(env, " ");
	while (arg) {
		if ((size_t)argc >= ARRAY_LEN(argvbuf) - 1)
			samu_fatal("too many arguments in SAMUFLAGS");
		argv[argc++] = arg;
		arg = strtok(NULL, " ");
	}
	argv[argc] = NULL;

	SAMU_ARGBEGIN {
	case 'j':
		samu_jobsflag(ctx, SAMU_EARGF(samu_usage(ctx)));
		break;
	case 'v':
		ctx->buildopts.verbose = true;
		break;
	default:
		samu_fatal("invalid option in SAMUFLAGS");
	} SAMU_ARGEND
}

static void
samu_init_ctx(struct samu_ctx *ctx, struct samu_opts *opts) {
	*ctx = (struct samu_ctx){
		.buildopts = {.maxfail = 1},
		.phonyrule = {.name = "phony"},
		.consolepool = {.name = "console", .maxjobs = 1},
		.out = stdout,
	};

	if (opts) {
		if (ctx->out) {
			ctx->out = opts->out;
		}
	}

	ctx->argv0 = "<muon samu>";

	samu_arena_init(&ctx->arena);
}

bool
samu_main(int argc, char *argv[], struct samu_opts *opts)
{
	char *builddir, *manifest = "build.ninja", *end, *arg;
	const struct samu_tool *tool = NULL;
	struct samu_node *n;
	long num;
	int tries;

	struct samu_ctx _ctx, *ctx = &_ctx;
	samu_init_ctx(ctx, opts);

	samu_parseenvargs(ctx, os_get_env("SAMUFLAGS"));
	SAMU_ARGBEGIN {
	case '-':
		arg = SAMU_EARGF(samu_usage(ctx));
		if (strcmp(arg, "version") == 0) {
			samu_printf(ctx, "%d.%d.0\n", ninjamajor, ninjaminor);
			return true;
		} else if (strcmp(arg, "verbose") == 0) {
			ctx->buildopts.verbose = true;
		} else {
			samu_usage(ctx);
		}
		break;
	case 'C':
		arg = SAMU_EARGF(samu_usage(ctx));
		/* samu_warn("entering directory '%s'", arg); */
		if (!path_chdir(arg))
			samu_fatal("chdir:");
		break;
	case 'd':
		samu_debugflag(ctx, SAMU_EARGF(samu_usage(ctx)));
		break;
	case 'f':
		manifest = SAMU_EARGF(samu_usage(ctx));
		break;
	case 'j':
		samu_jobsflag(ctx, SAMU_EARGF(samu_usage(ctx)));
		break;
	case 'k':
		num = strtol(SAMU_EARGF(samu_usage(ctx)), &end, 10);
		if (*end)
			samu_fatal("invalid -k parameter");
		ctx->buildopts.maxfail = num > 0 ? num : -1;
		break;
	case 'n':
		ctx->buildopts.dryrun = true;
		break;
	case 't':
		tool = samu_toolget(SAMU_EARGF(samu_toollist(ctx)));
		goto argdone;
	case 'v':
		ctx->buildopts.verbose = true;
		break;
	case 'w':
		samu_warnflag(ctx, SAMU_EARGF(samu_usage(ctx)));
		break;
	default:
		samu_usage(ctx);
	} SAMU_ARGEND
argdone:
	if (!ctx->buildopts.maxjobs) {
		ctx->buildopts.maxjobs = os_parallel_job_count();
	}

	ctx->buildopts.statusfmt = os_get_env("NINJA_STATUS");
	if (!ctx->buildopts.statusfmt)
		ctx->buildopts.statusfmt = "[%s/%t] ";

	tries = 0;
retry:
	/* (re-)initialize global graph, environment, and parse structures */
	samu_graphinit(ctx);
	samu_envinit(ctx);
	samu_parseinit(ctx);

	/* parse the manifest */
	samu_parse(ctx, manifest, ctx->env.rootenv);

	if (tool) {
		int r = tool->run(ctx, argc, argv);
		samu_arena_destroy(&ctx->arena);
		return r == 0;
	}

	/* load the build log */
	builddir = samu_getbuilddir(ctx);
	samu_loginit(ctx, builddir);
	samu_depsinit(ctx, builddir);

	/* rebuild the manifest if it's dirty */
	n = samu_nodeget(ctx, manifest, 0);
	if (n && n->gen) {
		samu_buildadd(ctx, n);
		if (n->dirty) {
			samu_build(ctx);
			if (n->gen->flags & FLAG_DIRTY_OUT || n->gen->nprune > 0) {
				if (++tries > 100)
					samu_fatal("manifest '%s' dirty after 100 tries", manifest);
				if (!ctx->buildopts.dryrun)
					goto retry;
			}
			/* manifest was pruned; reset state, then continue with build */
			samu_buildreset(ctx);
		}
	}

	/* finally, build any specified targets or the default targets */
	if (argc) {
		for (; *argv; ++argv) {
			n = samu_nodeget(ctx, *argv, 0);
			if (!n)
				samu_fatal("unknown target '%s'", *argv);
			samu_buildadd(ctx, n);
		}
	} else {
		samu_defaultnodes(ctx, samu_buildadd);
	}
	samu_build(ctx);
	samu_logclose(ctx);
	samu_depsclose(ctx);

	samu_arena_destroy(&ctx->arena);
	return true;
}

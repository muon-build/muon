/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  /* for chdir */

#include "buf_size.h"
#include "external/samurai/ctx.h"

#include "external/samurai/arg.h"
#include "external/samurai/build.h"
#include "external/samurai/deps.h"
#include "external/samurai/env.h"
#include "external/samurai/graph.h"
#include "external/samurai/log.h"
#include "external/samurai/parse.h"
#include "external/samurai/samu.h"
#include "external/samurai/tool.h"
#include "external/samurai/util.h"

static void
samu_usage(struct samu_ctx *ctx)
{
	fprintf(stderr, "usage: %s [-C dir] [-f buildfile] [-j maxjobs] [-k maxfail] [-l maxload] [-n]\n", ctx->argv0);
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
samu_loadflag(struct samu_ctx *ctx, const char *flag)
{
#ifdef NO_GETLOADAVG
	samu_warn("job scheduling based on load average is not implemented");
#else
	double value;
	char *end;
	errno = 0;

	value = strtod(flag, &end);
	if (*end || value < 0 || errno != 0)
		samu_fatal("invalid -l parameter");
	ctx->buildopts.maxload = value;
#endif
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
	if (*end || num < 0)
		samu_fatal("invalid -j parameter");
	ctx->buildopts.maxjobs = num > 0 ? num : -1;
}

static void
samu_parseenvargs(struct samu_ctx *ctx, char *env)
{
	char *arg, *argvbuf[64], **argv = argvbuf;
	int argc;

	if (!env)
		return;
	env = samu_xmemdup(&ctx->arena, env, strlen(env) + 1);
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
	case 'l':
		samu_loadflag(ctx, SAMU_EARGF(samu_usage(ctx)));
		break;
	default:
		samu_fatal("invalid option in SAMUFLAGS");
	} SAMU_ARGEND
}

static const char *
samu_progname(const char *arg, const char *def)
{
	const char *slash;

	if (!arg)
		return def;
	slash = strrchr(arg, '/');
	return slash ? slash + 1 : arg;
}

int
samu_main(int argc, char *argv[])
{
	char *builddir, *manifest = "build.ninja", *end, *arg;
	const struct samu_tool *tool = NULL;
	struct samu_node *n;
	long num;
	int tries;

	struct samu_ctx samu_ctx = {
		.buildopts = {.maxfail = 1},
		.phonyrule = {.name = "phony"},
		.consolepool = {.name = "console", .maxjobs = 1},
	}, *ctx = &samu_ctx;

	samu_arena_init(&ctx->arena);

	ctx->argv0 = samu_progname(argv[0], "samu");
	samu_parseenvargs(ctx, getenv("SAMUFLAGS"));
	SAMU_ARGBEGIN {
	case '-':
		arg = SAMU_EARGF(samu_usage(ctx));
		if (strcmp(arg, "version") == 0) {
			printf("%d.%d.0\n", ninjamajor, ninjaminor);
			return 0;
		} else if (strcmp(arg, "verbose") == 0) {
			ctx->buildopts.verbose = true;
		} else {
			samu_usage(ctx);
		}
		break;
	case 'C':
		arg = SAMU_EARGF(samu_usage(ctx));
		/* samu_warn("entering directory '%s'", arg); */
		if (chdir(arg) < 0)
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
	case 'l':
		samu_loadflag(ctx, SAMU_EARGF(samu_usage(ctx)));
		break;
	case 'n':
		ctx->buildopts.dryrun = true;
		break;
	case 't':
		tool = samu_toolget(SAMU_EARGF(samu_usage(ctx)));
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
#ifdef _SC_NPROCESSORS_ONLN
		int n = sysconf(_SC_NPROCESSORS_ONLN);
		switch (n) {
		case -1: case 0: case 1:
			ctx->buildopts.maxjobs = 2;
			break;
		case 2:
			ctx->buildopts.maxjobs = 3;
			break;
		default:
			ctx->buildopts.maxjobs = n + 2;
			break;
		}
#else
		ctx->buildopts.maxjobs = 2;
#endif
	}

	ctx->buildopts.statusfmt = getenv("NINJA_STATUS");
	if (!ctx->buildopts.statusfmt)
		ctx->buildopts.statusfmt = "[%s/%t] ";

	setvbuf(stdout, NULL, _IOLBF, 0);

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
		return r;
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
	return 0;
}

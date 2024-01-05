/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#ifndef NO_GETLOADAVG
#define _BSD_SOURCE /* for getloadavg */
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "external/samurai/ctx.h"

#include "external/samurai/build.h"
#include "external/samurai/deps.h"
#include "external/samurai/env.h"
#include "external/samurai/graph.h"
#include "external/samurai/log.h"
#include "external/samurai/util.h"

struct samu_job {
	struct samu_string *cmd;
	struct samu_edge *edge;
	struct samu_buffer buf;
	size_t next;
	pid_t pid;
	int fd;
	bool failed;
};

void
samu_buildreset(struct samu_ctx *ctx)
{
	struct samu_edge *e;

	for (e = ctx->graph.alledges; e; e = e->allnext)
		e->flags &= ~FLAG_WORK;
}

/* returns whether n1 is newer than n2, or false if n1 is NULL */
static bool
samu_isnewer(struct samu_node *n1, struct samu_node *n2)
{
	return n1 && n1->mtime > n2->mtime;
}

/* returns whether this output node is dirty in relation to the newest input */
static bool
samu_isdirty(struct samu_ctx *ctx, struct samu_node *n, struct samu_node *newest, bool generator, bool restat)
{
	struct samu_edge *e;

	e = n->gen;
	if (e->rule == &ctx->phonyrule) {
		if (e->nin > 0 || n->mtime != SAMU_MTIME_MISSING)
			return false;
		if (ctx->buildopts.explain)
			samu_warn("explain %s: phony and no inputs", n->path->s);
		return true;
	}
	if (n->mtime == SAMU_MTIME_MISSING) {
		if (ctx->buildopts.explain)
			samu_warn("explain %s: missing", n->path->s);
		return true;
	}
	if (samu_isnewer(newest, n) && (!restat || n->logmtime == SAMU_MTIME_MISSING)) {
		if (ctx->buildopts.explain) {
			samu_warn("explain %s: older than input '%s': %" PRId64 " vs %" PRId64,
			     n->path->s, newest->path->s, n->mtime, newest->mtime);
		}
		return true;
	}
	if (n->logmtime == SAMU_MTIME_MISSING) {
		if (!generator) {
			if (ctx->buildopts.explain)
				samu_warn("explain %s: no record in .ninja_log", n->path->s);
			return true;
		}
	} else if (newest && n->logmtime < newest->mtime) {
		if (ctx->buildopts.explain) {
			samu_warn("explain %s: recorded mtime is older than input '%s': %" PRId64 " vs %" PRId64,
			     n->path->s, newest->path->s, n->logmtime, newest->mtime);
		}
		return true;
	}
	if (generator)
		return false;
	samu_edgehash(ctx, e);
	if (e->hash == n->hash)
		return false;
	if (ctx->buildopts.explain)
		samu_warn("explain %s: command line changed", n->path->s);
	return true;
}

/* add an edge to the work queue */
static void
samu_queue(struct samu_ctx *ctx, struct samu_edge *e)
{
	struct samu_edge **front = &ctx->build.work;

	if (e->pool && e->rule != &ctx->phonyrule) {
		if (e->pool->numjobs == e->pool->maxjobs)
			front = &e->pool->work;
		else
			++e->pool->numjobs;
	}
	e->worknext = *front;
	*front = e;
}

void
samu_buildadd(struct samu_ctx *ctx, struct samu_node *n)
{
	struct samu_edge *e;
	struct samu_node *newest;
	size_t i;
	bool generator, restat;

	e = n->gen;
	if (!e) {
		if (n->mtime == SAMU_MTIME_UNKNOWN)
			samu_nodestat(n);
		if (n->mtime == SAMU_MTIME_MISSING)
			samu_fatal("file is missing and not created by any action: '%s'", n->path->s);
		n->dirty = false;
		return;
	}
	if (e->flags & FLAG_CYCLE)
		samu_fatal("dependency cycle involving '%s'", n->path->s);
	if (e->flags & FLAG_WORK)
		return;
	e->flags |= FLAG_CYCLE | FLAG_WORK;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		n->dirty = false;
		if (n->mtime == SAMU_MTIME_UNKNOWN)
			samu_nodestat(n);
	}
	samu_depsload(ctx, e);
	e->nblock = 0;
	newest = NULL;
	for (i = 0; i < e->nin; ++i) {
		n = e->in[i];
		samu_buildadd(ctx, n);
		if (i < e->inorderidx) {
			if (n->dirty)
				e->flags |= FLAG_DIRTY_IN;
			if (n->mtime != SAMU_MTIME_MISSING && !samu_isnewer(newest, n))
				newest = n;
		}
		if (n->dirty || (n->gen && n->gen->nblock > 0))
			++e->nblock;
	}
	/* all outputs are dirty if any are older than the newest input */
	generator = samu_edgevar(ctx, e, "generator", true);
	restat = samu_edgevar(ctx, e, "restat", true);
	for (i = 0; i < e->nout && !(e->flags & FLAG_DIRTY_OUT); ++i) {
		n = e->out[i];
		if (samu_isdirty(ctx, n, newest, generator, restat)) {
			n->dirty = true;
			e->flags |= FLAG_DIRTY_OUT;
		}
	}
	if (e->flags & FLAG_DIRTY) {
		for (i = 0; i < e->nout; ++i) {
			n = e->out[i];
			if (ctx->buildopts.explain && !n->dirty) {
				if (e->flags & FLAG_DIRTY_IN)
					samu_warn("explain %s: input is dirty", n->path->s);
				else if (e->flags & FLAG_DIRTY_OUT)
					samu_warn("explain %s: output of generating action is dirty", n->path->s);
			}
			n->dirty = true;
		}
	}
	if (!(e->flags & FLAG_DIRTY_OUT))
		e->nprune = e->nblock;
	if (e->flags & FLAG_DIRTY) {
		if (e->nblock == 0)
			samu_queue(ctx, e);
		if (e->rule != &ctx->phonyrule)
			++ctx->build.ntotal;
	}
	e->flags &= ~FLAG_CYCLE;
}

static size_t
samu_formatstatus(struct samu_ctx *ctx, char *buf, size_t len)
{
	const char *fmt;
	size_t ret = 0;
	int n;

	for (fmt = ctx->buildopts.statusfmt; *fmt; ++fmt) {
		if (*fmt != '%' || *++fmt == '%') {
			if (len > 1) {
				*buf++ = *fmt;
				--len;
			}
			++ret;
			continue;
		}
		n = 0;
		switch (*fmt) {
		case 's':
			n = snprintf(buf, len, "%zu", ctx->build.nstarted);
			break;
		case 'f':
			n = snprintf(buf, len, "%zu", ctx->build.nfinished);
			break;
		case 't':
			n = snprintf(buf, len, "%zu", ctx->build.ntotal);
			break;
		case 'r':
			n = snprintf(buf, len, "%zu", ctx->build.nstarted - ctx->build.nfinished);
			break;
		case 'u':
			n = snprintf(buf, len, "%zu", ctx->build.ntotal - ctx->build.nstarted);
			break;
		case 'p':
			n = snprintf(buf, len, "%3zu%%", 100 * ctx->build.nfinished / ctx->build.ntotal);
			break;
		case 'o':
			n = snprintf(buf, len, "%.1f", ctx->build.nfinished / timer_read(&ctx->build.timer));
			break;
		case 'e':
			n = snprintf(buf, len, "%.3f", timer_read(&ctx->build.timer));
			break;
		default:
			samu_fatal("unknown placeholder '%%%c' in $NINJA_STATUS", *fmt);
			continue;  /* unreachable, but avoids warning */
		}
		if (n < 0)
			samu_fatal("snprintf:");
		ret += n;
		if ((size_t)n > len)
			n = len;
		buf += n;
		len -= n;
	}
	if (len > 0)
		*buf = '\0';
	return ret;
}

static void
samu_printstatus(struct samu_ctx *ctx, struct samu_edge *e, struct samu_string *cmd)
{
	struct samu_string *description;
	char status[256];

	description = ctx->buildopts.verbose ? NULL : samu_edgevar(ctx, e, "description", true);
	if (!description || description->n == 0)
		description = cmd;
	samu_formatstatus(ctx, status, sizeof(status));
	fputs(status, stdout);
	puts(description->s);
}

static int
samu_jobstart(struct samu_ctx *ctx, struct samu_job *j, struct samu_edge *e)
{
	extern char **environ;
	size_t i;
	struct samu_node *n;
	struct samu_string *rspfile, *content;
	int fd[2];
	posix_spawn_file_actions_t actions;
	char *argv[] = {"/bin/sh", "-c", NULL, NULL};

	++ctx->build.nstarted;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		if (n->mtime == SAMU_MTIME_MISSING) {
			if (samu_makedirs(n->path, true) < 0)
				goto err0;
		}
	}
	rspfile = samu_edgevar(ctx, e, "rspfile", false);
	if (rspfile) {
		content = samu_edgevar(ctx, e, "rspfile_content", true);
		if (samu_writefile(rspfile->s, content) < 0)
			goto err0;
	}

	if (pipe(fd) < 0) {
		samu_warn("pipe:");
		goto err1;
	}
	j->edge = e;
	j->cmd = samu_edgevar(ctx, e, "command", true);
	j->fd = fd[0];
	argv[2] = j->cmd->s;

	if (!ctx->build.consoleused)
		samu_printstatus(ctx, e, j->cmd);

	if ((errno = posix_spawn_file_actions_init(&actions))) {
		samu_warn("posix_spawn_file_actions_init:");
		goto err2;
	}
	if ((errno = posix_spawn_file_actions_addclose(&actions, fd[0]))) {
		samu_warn("posix_spawn_file_actions_addclose:");
		goto err3;
	}
	if (e->pool != &ctx->consolepool) {
		if ((errno = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0))) {
			samu_warn("posix_spawn_file_actions_addopen:");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 1))) {
			samu_warn("posix_spawn_file_actions_adddup2:");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 2))) {
			samu_warn("posix_spawn_file_actions_adddup2:");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_addclose(&actions, fd[1]))) {
			samu_warn("posix_spawn_file_actions_addclose:");
			goto err3;
		}
	}
	if ((errno = posix_spawn(&j->pid, argv[0], &actions, NULL, argv, environ))) {
		samu_warn("posix_spawn %s:", j->cmd->s);
		goto err3;
	}
	posix_spawn_file_actions_destroy(&actions);
	close(fd[1]);
	j->failed = false;
	if (e->pool == &ctx->consolepool)
		ctx->build.consoleused = true;

	return j->fd;

err3:
	posix_spawn_file_actions_destroy(&actions);
err2:
	close(fd[0]);
	close(fd[1]);
err1:
	if (rspfile && !ctx->buildopts.keeprsp)
		remove(rspfile->s);
err0:
	return -1;
}

static void
samu_nodedone(struct samu_ctx *ctx, struct samu_node *n, bool prune)
{
	struct samu_edge *e;
	size_t i, j;

	for (i = 0; i < n->nuse; ++i) {
		e = n->use[i];
		/* skip edges not used in this build */
		if (!(e->flags & FLAG_WORK))
			continue;
		if (!(e->flags & (prune ? FLAG_DIRTY_OUT : FLAG_DIRTY)) && --e->nprune == 0) {
			/* either edge was clean (possible with order-only
			 * inputs), or all its blocking inputs were pruned, so
			 * its outputs can be pruned as well */
			for (j = 0; j < e->nout; ++j)
				samu_nodedone(ctx, e->out[j], true);
			if (e->flags & FLAG_DIRTY && e->rule != &ctx->phonyrule)
				--ctx->build.ntotal;
		} else if (--e->nblock == 0) {
			samu_queue(ctx, e);
		}
	}
}

static bool
samu_shouldprune(struct samu_edge *e, struct samu_node *n, int64_t old)
{
	struct samu_node *in, *newest;
	size_t i;

	if (old != n->mtime)
		return false;
	newest = NULL;
	for (i = 0; i < e->inorderidx; ++i) {
		in = e->in[i];
		samu_nodestat(in);
		if (in->mtime != SAMU_MTIME_MISSING && !samu_isnewer(newest, in))
			newest = in;
	}
	if (newest)
		n->logmtime = newest->mtime;

	return true;
}

static void
samu_edgedone(struct samu_ctx *ctx, struct samu_edge *e)
{
	struct samu_node *n;
	size_t i;
	struct samu_string *rspfile;
	bool restat;
	int64_t old;

	restat = samu_edgevar(ctx, e, "restat", true);
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		old = n->mtime;
		samu_nodestat(n);
		n->logmtime = n->mtime == SAMU_MTIME_MISSING ? 0 : n->mtime;
		samu_nodedone(ctx, n, restat && samu_shouldprune(e, n, old));
	}
	rspfile = samu_edgevar(ctx, e, "rspfile", false);
	if (rspfile && !ctx->buildopts.keeprsp)
		remove(rspfile->s);
	samu_edgehash(ctx, e);
	samu_depsrecord(ctx, e);
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		n->hash = e->hash;
		samu_logrecord(ctx, n);
	}
}

static void
samu_jobdone(struct samu_ctx *ctx, struct samu_job *j)
{
	int status;
	struct samu_edge *e, *new;
	struct samu_pool *p;

	++ctx->build.nfinished;
	if (waitpid(j->pid, &status, 0) < 0) {
		samu_warn("waitpid %d:", j->pid);
		j->failed = true;
	} else if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			samu_warn("job failed: %s", j->cmd->s);
			j->failed = true;
		}
	} else if (WIFSIGNALED(status)) {
		samu_warn("job terminated due to signal %d: %s", WTERMSIG(status), j->cmd->s);
		j->failed = true;
	} else {
		/* cannot happen according to POSIX */
		samu_warn("job status unknown: %s", j->cmd->s);
		j->failed = true;
	}
	close(j->fd);
	if (j->buf.len && (!ctx->build.consoleused || j->failed))
		fwrite(j->buf.data, 1, j->buf.len, stdout);
	j->buf.len = 0;
	e = j->edge;
	if (e->pool) {
		p = e->pool;

		if (p == &ctx->consolepool)
			ctx->build.consoleused = false;
		/* move edge from pool queue to main work queue */
		if (p->work) {
			new = p->work;
			p->work = p->work->worknext;
			new->worknext = ctx->build.work;
			ctx->build.work = new;
		} else {
			--p->numjobs;
		}
	}
	if (!j->failed)
		samu_edgedone(ctx, e);
}

/* returns whether a job still has work to do. if not, sets j->failed */
static bool
samu_jobwork(struct samu_ctx *ctx, struct samu_job *j)
{
	char *newdata;
	size_t newcap;
	ssize_t n;

	if (j->buf.cap - j->buf.len < BUFSIZ / 2) {
		newcap = j->buf.cap + BUFSIZ;
		newdata = samu_arena_realloc(&ctx->arena, j->buf.data, j->buf.cap, newcap);
		if (!newdata) {
			samu_warn("realloc:");
			goto kill;
		}
		j->buf.cap = newcap;
		j->buf.data = newdata;
	}
	n = read(j->fd, j->buf.data + j->buf.len, j->buf.cap - j->buf.len);
	if (n > 0) {
		j->buf.len += n;
		return true;
	}
	if (n == 0)
		goto done;
	samu_warn("read:");

kill:
	kill(j->pid, SIGTERM);
	j->failed = true;
done:
	samu_jobdone(ctx, j);

	return false;
}

/* queries the system load average */
static double
samu_queryload(void)
{
#ifdef NO_GETLOADAVG
	return 0;
#else
	double load;

	if (getloadavg(&load, 1) == -1) {
		samu_warn("getloadavg:");
		load = 100.0;
	}

	return load;
#endif
}

void
samu_build(struct samu_ctx *ctx)
{
	struct samu_job *jobs = NULL;
	struct pollfd *fds = NULL;
	size_t i, next = 0, jobslen = 0, maxjobs = ctx->buildopts.maxjobs, numjobs = 0, numfail = 0;
	struct samu_edge *e;

	if (ctx->build.ntotal == 0) {
		return;
	}

	timer_start(&ctx->build.timer);
	samu_formatstatus(ctx, NULL, 0);

	ctx->build.nstarted = 0;
	for (;;) {
		/* limit number of of jobs based on load */
		if (ctx->buildopts.maxload)
			maxjobs = samu_queryload() > ctx->buildopts.maxload ? 1 : ctx->buildopts.maxjobs;
		/* start ready edges */
		while (ctx->build.work && numjobs < maxjobs && numfail < ctx->buildopts.maxfail) {
			e = ctx->build.work;
			ctx->build.work = ctx->build.work->worknext;
			if (e->rule != &ctx->phonyrule && ctx->buildopts.dryrun) {
				++ctx->build.nstarted;
				samu_printstatus(ctx, e, samu_edgevar(ctx, e, "command", true));
				++ctx->build.nfinished;
			}
			if (e->rule == &ctx->phonyrule || ctx->buildopts.dryrun) {
				for (i = 0; i < e->nout; ++i)
					samu_nodedone(ctx, e->out[i], false);
				continue;
			}
			if (next == jobslen) {
				size_t newjobslen;
				newjobslen = jobslen ? jobslen * 2 : 8;
				if (newjobslen > ctx->buildopts.maxjobs)
					newjobslen = ctx->buildopts.maxjobs;
				jobs = samu_xreallocarray(&ctx->arena, jobs, jobslen, newjobslen, sizeof(jobs[0]));
				fds = samu_xreallocarray(&ctx->arena, fds, jobslen, newjobslen, sizeof(fds[0]));
				jobslen = newjobslen;
				for (i = next; i < jobslen; ++i) {
					jobs[i].buf.data = NULL;
					jobs[i].buf.len = 0;
					jobs[i].buf.cap = 0;
					jobs[i].next = i + 1;
					fds[i].fd = -1;
					fds[i].events = POLLIN;
				}
			}
			fds[next].fd = samu_jobstart(ctx, &jobs[next], e);
			if (fds[next].fd < 0) {
				samu_warn("job failed to start");
				++numfail;
			} else {
				next = jobs[next].next;
				++numjobs;
			}
		}
		if (numjobs == 0)
			break;
		if (poll(fds, jobslen, 5000) < 0)
			samu_fatal("poll:");
		for (i = 0; i < jobslen; ++i) {
			if (!fds[i].revents || samu_jobwork(ctx, &jobs[i]))
				continue;
			--numjobs;
			jobs[i].next = next;
			fds[i].fd = -1;
			next = i;
			if (jobs[i].failed)
				++numfail;
		}
	}
	if (numfail > 0) {
		if (numfail < ctx->buildopts.maxfail)
			samu_fatal("cannot make progress due to previous errors");
		else if (numfail > 1)
			samu_fatal("subcommands failed");
		else
			samu_fatal("subcommand failed");
	}
	ctx->build.ntotal = 0;  /* reset in case we just rebuilt the manifest */
}

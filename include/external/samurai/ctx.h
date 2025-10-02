/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_CTX_H
#define MUON_EXTERNAL_SAMU_CTX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "platform/filesystem.h"
#include "platform/timer.h"

struct samu_buffer {
	char *data;
	size_t len, cap;
};

struct samu_string {
	size_t n;
	char s[];
};

/* an unevaluated string */
struct samu_evalstring {
	char *var;
	struct samu_string *str;
	struct samu_evalstring *next;
};

struct samu_hashtablekey {
	uint64_t hash;
	const char *str;
	size_t len;
};

struct samu_buildoptions {
	size_t maxjobs, maxfail;
	_Bool verbose, explain, keepdepfile, keeprsp, dryrun;
	const char *statusfmt;
};

struct samu_parseoptions {
	_Bool dupbuildwarn;
};

enum samu_token {
	SAMU_BUILD,
	SAMU_DEFAULT,
	SAMU_INCLUDE,
	SAMU_POOL,
	SAMU_RULE,
	SAMU_SUBNINJA,
	SAMU_VARIABLE,
};

struct samu_scanner {
	struct source src;
	const char *path;
	int chr, line, col;
	uint32_t src_i;
};

struct samu_node {
	/* shellpath is the escaped shell path, and is populated as needed by nodepath */
	struct samu_string *path, *shellpath;

	/* modification time of file (in nanoseconds) and build log entry (in seconds) */
	int64_t mtime, logmtime;

	/* generating edge and dependent edges */
	struct samu_edge *gen, **use;
	size_t nuse;

	/* command hash used to build this output, read from build log */
	uint64_t hash;

	/* ID for .ninja_deps. -1 if not present in log. */
	int32_t id;

	/* does the node need to be rebuilt */
	_Bool dirty;
};

/* build rule, i.e., edge between inputs and outputs */
struct samu_edge {
	struct samu_rule *rule;
	struct samu_pool *pool;
	struct samu_environment *env;

	/* input and output nodes */
	struct samu_node **out, **in;
	size_t nout, nin;

	/* index of first implicit output */
	size_t outimpidx;
	/* index of first implicit and order-only input */
	size_t inimpidx, inorderidx;

	/* command hash */
	uint64_t hash;

	/* how many inputs need to be rebuilt or pruned before this edge is ready */
	size_t nblock;
	/* how many inputs need to be pruned before all outputs can be pruned */
	size_t nprune;

	enum {
		FLAG_WORK      = 1 << 0,  /* scheduled for build */
		FLAG_HASH      = 1 << 1,  /* calculated the command hash */
		FLAG_DIRTY_IN  = 1 << 3,  /* dirty input */
		FLAG_DIRTY_OUT = 1 << 4,  /* missing or outdated output */
		FLAG_DIRTY     = FLAG_DIRTY_IN | FLAG_DIRTY_OUT,
		FLAG_CYCLE     = 1 << 5,  /* used for cycle detection */
		FLAG_DEPS      = 1 << 6,  /* dependencies loaded */
	} flags;

	/* used to coordinate ready work in build() */
	struct samu_edge *worknext;
	/* used for alledges linked list */
	struct samu_edge *allnext;
};

struct samu_nodearray {
	struct samu_node **node;
	size_t len;
};

struct samu_entry {
	struct samu_node *node;
	struct samu_nodearray deps;
	int64_t mtime;
};

struct samu_rule {
	char *name;
	struct samu_treenode *bindings;
};

struct samu_pool {
	char *name;
	int numjobs, maxjobs;

	/* a queue of ready edges blocked by the pool's capacity */
	struct samu_edge *work;
};

struct samu_build_ctx {
	struct samu_edge *work;
	size_t nstarted, nfinished, ntotal;
	bool consoleused;
	struct timer timer;
};

struct samu_deps_ctx {
	FILE *depsfile;
	struct samu_entry *entries;
	size_t entrieslen, entriescap;

	struct samu_buffer buf;
	struct samu_nodearray deps;
	size_t depscap;
};

struct samu_env_ctx {
	struct samu_environment *rootenv;
	struct samu_treenode *pools;
	struct samu_environment *allenvs;
};

struct samu_graph_ctx {
	struct samu_hashtable *allnodes;
	struct samu_edge *alledges;
};

struct samu_log_ctx {
	FILE *logfile;
};

struct samu_parse_ctx {
	struct samu_node **deftarg;
	size_t ndeftarg;
};

struct samu_scan_ctx {
	struct samu_evalstring **paths;
	size_t npaths;
	size_t paths_max;
	struct samu_buffer buf;
};

struct samu_ctx {
	struct samu_buildoptions buildopts;
	struct samu_parseoptions parseopts;

	struct samu_build_ctx build;
	struct samu_deps_ctx deps;
	struct samu_env_ctx env;
	struct samu_graph_ctx graph;
	struct samu_log_ctx log;
	struct samu_parse_ctx parse;
	struct samu_scan_ctx scan;

	const char *argv0;
	struct samu_rule phonyrule;
	struct samu_pool consolepool;
	struct arena *a;

	struct workspace *wk;
	FILE *out;
};

struct samu_tool {
	const char *name;
	int (*run)(struct samu_ctx *, int, char *[]);
};
#endif

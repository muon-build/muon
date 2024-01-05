/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_GRAPH_H
#define MUON_EXTERNAL_SAMU_GRAPH_H

#include <stdint.h>  /* for uint64_t */

struct samu_string;

/* set in the tv_nsec field of a node's mtime */
enum {
	/* we haven't stat the file yet */
	SAMU_MTIME_UNKNOWN = -1,
	/* the file does not exist */
	SAMU_MTIME_MISSING = -2,
};

void samu_graphinit(struct samu_ctx *ctx);

/* create a new node or return existing node */
struct samu_node *samu_mknode(struct samu_ctx *ctx, struct samu_string *path);
/* lookup a node by name; returns NULL if it does not exist */
struct samu_node *samu_nodeget(struct samu_ctx *ctx, const char *path, size_t len);
/* update the mtime field of a node */
void samu_nodestat(struct samu_node *);
/* get a node's path, possibly escaped for the shell */
struct samu_string *samu_nodepath(struct samu_ctx *ctx, struct samu_node *n, bool escape);
/* record the usage of a node by an edge */
void samu_nodeuse(struct samu_ctx *ctx, struct samu_node *n, struct samu_edge *e);

/* create a new edge with the given parent environment */
struct samu_edge *samu_mkedge(struct samu_ctx *ctx, struct samu_environment *parent);
/* compute the murmurhash64a of an edge command and store it in the hash field */
void samu_edgehash(struct samu_ctx *ctx, struct samu_edge *e);
/* add dependencies from $depfile or .ninja_deps as implicit inputs */
void samu_edgeadddeps(struct samu_ctx *ctx, struct samu_edge *e, struct samu_node **deps, size_t ndeps);

#endif

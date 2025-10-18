/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: 2005-2014 Rich Felker, et al.
 * SPDX-License-Identifier: MIT
 *
 * Based on musl's src/search/tsearch.c, by Szabolcs Nagy.
 */

#include "compat.h"

#include <string.h>

#include "external/samurai/ctx.h"

#include "external/samurai/tree.h"
#include "external/samurai/util.h"

#include "external/samurai/ctx.h"

#define MAXH (sizeof(void *) * 8 * 3 / 2)

static inline int
samu_height(struct samu_treenode *n)
{
	return n ? n->height : 0;
}

static int
samu_rot(struct samu_treenode **p, struct samu_treenode *x, int dir /* deeper side */)
{
	struct samu_treenode *y = x->child[dir];
	struct samu_treenode *z = y->child[!dir];
	int hx = x->height;
	int hz = samu_height(z);

	if (hz > samu_height(y->child[dir])) {
		/*
		 *   x
		 *  / \ dir          z
		 * A   y            / \
		 *    / \   -->    x   y
		 *   z   D        /|   |\
		 *  / \          A B   C D
		 * B   C
		 */
		x->child[dir] = z->child[!dir];
		y->child[!dir] = z->child[dir];
		z->child[!dir] = x;
		z->child[dir] = y;
		x->height = hz;
		y->height = hz;
		z->height = hz + 1;
	} else {
		/*
		 *   x               y
		 *  / \             / \
		 * A   y    -->    x   D
		 *    / \         / \
		 *   z   D       A   z
		 */
		x->child[dir] = z;
		y->child[!dir] = x;
		x->height = hz + 1;
		y->height = hz + 2;
		z = y;
	}
	*p = z;
	return z->height - hx;
}

static int
samu_balance(struct samu_treenode **p)
{
	struct samu_treenode *n = *p;
	int h0 = samu_height(n->child[0]);
	int h1 = samu_height(n->child[1]);

	if (h0 - h1 + 1u < 3u) {
		int old = n->height;
		n->height = h0 < h1 ? h1 + 1 : h0 + 1;
		return n->height - old;
	}
	return samu_rot(p, n, h0 < h1);
}

struct samu_treenode *
samu_treefind(struct samu_treenode *n, const char *key)
{
	int c;

	while (n) {
		c = strcmp(key, n->key);
		if (c == 0)
			return n;
		n = n->child[c > 0];
	}
	return NULL;
}

void *
samu_treeinsert(struct samu_ctx *ctx, struct samu_treenode **rootp, char *key, void *value)
{
	struct samu_treenode **a[MAXH], *n = *rootp, *r;
	void *old;
	int i = 0, c;

	a[i++] = rootp;
	while (n) {
		c = strcmp(key, n->key);
		if (c == 0) {
			old = n->value;
			n->value = value;
			return old;
		}
		a[i++] = &n->child[c > 0];
		n = n->child[c > 0];
	}
	r = samu_xmalloc(ctx->a, sizeof(*r));
	r->key = key;
	r->value = value;
	r->child[0] = r->child[1] = NULL;
	r->height = 1;
	/* insert new node, rebalance ancestors.  */
	*a[--i] = r;
	while (i && samu_balance(a[--i]))
		;
	return NULL;
}

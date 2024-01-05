/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_TREE_H
#define MUON_EXTERNAL_SAMU_TREE_H
/* binary tree node, such that keys are sorted lexicographically for fast lookup */
struct samu_treenode {
	char *key;
	void *value;
	struct samu_treenode *child[2];
	int height;
};

/* search a binary tree for a key, return the key's value or NULL */
struct samu_treenode *samu_treefind(struct samu_treenode *, const char *);
/* insert into a binary tree a key and a value, replace and return the old value if the key already exists */
void *samu_treeinsert(struct samu_ctx *ctx, struct samu_treenode **rootp, char *key, void *value);
#endif

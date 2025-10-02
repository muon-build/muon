/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_HTAB_H
#define MUON_EXTERNAL_SAMU_HTAB_H

#include <stdint.h>  /* for uint64_t */

void samu_htabkey(struct samu_hashtablekey *, const char *, size_t);

struct samu_hashtable *samu_mkhtab(struct arena *a, size_t cap);
void **samu_htabput(struct arena *a, struct samu_hashtable *h, struct samu_hashtablekey *k);
void *samu_htabget(struct samu_hashtable *, struct samu_hashtablekey *);

uint64_t samu_murmurhash64a(const void *, size_t);

#endif

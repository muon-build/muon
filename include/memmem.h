/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_MEMMEM_H
#define MUON_MEMMEM_H
#include <stddef.h>

void *memmem(const void *h0, size_t k, const void *n0, size_t l);
#endif

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_RPMVERCMP_H
#define MUON_RPMVERCMP_H
#include "lang/string.h"

int8_t rpmvercmp(const struct str *a, const struct str *b);
#endif

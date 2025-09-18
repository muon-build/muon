/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_UTIL_H
#define MUON_UTIL_H

#ifdef MIN
#undef MIN
#endif

#ifdef MAX
#undef MAX
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define IS_POWER_OF_TWO(__i) ((__i & (__i - 1)) == 0)

#endif

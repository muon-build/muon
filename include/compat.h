/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#if defined(__GNUC__)
#define MUON_ATTR_FORMAT(type, start, end) __attribute__((format(type, start, end)))
#else
#define MUON_ATTR_FORMAT(type, start, end)
#endif

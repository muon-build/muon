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

#ifdef __OpenBSD__
#include <stdarg.h>
#endif

#if !defined(__SANITIZE_ADDRESS__) && defined(__has_feature)
#if __has_feature(address_sanitizer)
#define __SANITIZE_ADDRESS__
#endif
#endif

#if !defined(__SANITIZE_UNDEFINED__) && defined(__has_feature)
#if __has_feature(undefined_behavior_sanitizer)
#define __SANITIZE_UNDEFINED__
#endif
#endif

#if !defined(__SANITIZE_MEMORY__) && defined(__has_feature)
#if __has_feature(memory_sanitizer)
#define __SANITIZE_MEMORY__
#endif
#endif

#ifndef MUON_RELEASE
#define MUON_RELEASE 0
#endif

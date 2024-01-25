/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_WINDOWS_WIN32_ERROR_H
#define MUON_PLATFORM_WINDOWS_WIN32_ERROR_H

#include "compat.h"

const char *win32_error(void);
void win32_fatal(const char *fmt, ...) MUON_ATTR_FORMAT(printf, 1, 2);

#endif

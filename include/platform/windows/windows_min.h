/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_WINDOWS_MIN_H
#define MUON_PLATFORM_WINDOWS_MIN_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOGDI
#define NOGDI
#endif

#ifndef NOUSER
#define NOUSER
#endif

#ifndef NOAPISET
#define NOAPISET
#endif

#ifndef NOATOM
#define NOATOM
#endif

#ifndef NOCLIPBOARD
#define NOCLIPBOARD
#endif

#ifndef NODRAWTEXT
#define NODRAWTEXT
#endif

#ifndef NOSERVICE
#define NOSERVICE
#endif

#ifndef NOSOUND
#define NOSOUND
#endif

#include <windows.h>

#endif

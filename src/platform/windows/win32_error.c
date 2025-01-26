/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define STRSAFE_NO_CB_FUNCTIONS
#include <strsafe.h>
#include <stdlib.h>

#include "lang/string.h"
#include "log.h"
#include "platform/windows/win32_error.h"

const char *
win32_error(void)
{
	static char _msg[4096];

	LPTSTR msg;
	DWORD err;

	err = GetLastError();

	if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		    NULL,
		    err,
		    0UL,
		    (LPTSTR)&msg,
		    0UL,
		    NULL)) {
		snprintf(_msg, sizeof(_msg), "FormatMessage() failed with error Id %ld", GetLastError());
		return _msg;
	}

	// strip trailing newlines from the error message
	char *end = &msg[strlen(msg) - 1];
	while (end > msg && is_whitespace(*end)) {
		*end = 0;
		--end;
	}

	snprintf(_msg, sizeof(_msg), "%s (%lu)", msg, err);

	LocalFree(msg);

	return _msg;
}

void
win32_fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_plainv(fmt, ap);
	va_end(ap);
	if (fmt[strlen(fmt) - 1] == ':') {
		log_plain(" %s", win32_error());
	}
	log_plain("\n");
	exit(1);
}

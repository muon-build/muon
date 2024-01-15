/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <unistd.h>

#ifdef MUON_HAVE_GETLOADAVG
#include <errno.h>
#include <string.h>

#if defined(__APPLE__)
// On macOS, getloadavg is unavailable if _POSIX_C_SOURCE is defined
#undef _POSIX_C_SOURCE
#else
// Otherwise assume getloadavg is available when _BSD_SOURCE is defined
#define _BSD_SOURCE
#endif

#include <stdlib.h>

#endif

#include "log.h"
#include "platform/os.h"

bool os_chdir(const char *path)
{
	return chdir(path) == 0;
}

char *os_getcwd(char *buf, size_t size)
{
	return getcwd(buf, size);
}

int os_getopt(int argc, char * const argv[], const char *optstring)
{
	return getopt(argc, argv, optstring);
}

int32_t
os_ncpus(void)
{
#ifdef _SC_NPROCESSORS_ONLN
	return sysconf(_SC_NPROCESSORS_ONLN);
#else
	return -1;
#endif
}

double
os_getloadavg(void)
{
#ifdef MUON_HAVE_GETLOADAVG
	double load;

	if (getloadavg(&load, 1) == -1) {
		LOG_W("failed: getloadavg: %s", strerror(errno));
		load = 100.0;
	}

	return load;
#else
	return 0;
#endif
}

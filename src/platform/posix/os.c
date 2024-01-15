/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <unistd.h>
#include <errno.h>
#include <string.h>

// Messing with these defines and includes in the amalgamated build is
// difficult to reason about, so features requiring feature detection are
// disabled until we are bootstrapped.
#if defined(MUON_BOOTSTRAPPED)

#if defined(__APPLE__)
// On macOS, getloadavg is unavailable if _POSIX_C_SOURCE is defined
#undef _POSIX_C_SOURCE
// for sysctl
#include <sys/types.h>
#include <sys/sysctl.h>
#endif // defined(__APPLE__)

#ifdef MUON_HAVE_GETLOADAVG
#if !defined(__APPLE__)
// Assume getloadavg is available when _BSD_SOURCE is defined
#define _BSD_SOURCE
#endif
#include <stdlib.h>
#endif // MUON_HAVE_GETLOADAVG

#endif // MUON_BOOTSTRAPPED

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
#if defined(_SC_NPROCESSORS_ONLN)
	return sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(__APPLE__) && defined(MUON_BOOTSTRAPPED)
	int64_t res;
	size_t size = sizeof(res);
	int r = sysctlbyname("hw.activecpu", &res, &size, NULL, 0);
	if (r == -1) {
		return -1;
	} else {
		return res;
	}
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

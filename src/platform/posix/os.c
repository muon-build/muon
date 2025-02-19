/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__) && defined(MUON_BOOTSTRAPPED)
#undef _POSIX_C_SOURCE
// for sysctl
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include "lang/string.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/os.h"

bool
os_chdir(const char *path)
{
	return chdir(path) == 0;
}

char *
os_getcwd(char *buf, size_t size)
{
	return getcwd(buf, size);
}

int
os_getopt(int argc, char *const argv[], const char *optstring)
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

void
os_set_env(const struct str *k, const struct str *v)
{
	TSTR_manual(buf_k);
	TSTR_manual(buf_v);

	tstr_pushn(0, &buf_k, k->s, k->len);
	tstr_push(0, &buf_k, 0);
	tstr_pushn(0, &buf_v, v->s, v->len);
	tstr_push(0, &buf_v, 0);

	setenv(buf_k.buf, buf_v.buf, true);
}

bool
os_is_debugger_attached(void)
{
#if defined(__APPLE__) && defined(MUON_BOOTSTRAPPED)
	// From Apple Technical Q&A QA1361
	// https://developer.apple.com/library/archive/qa/qa1361/_index.html
	int junk;
	int mib[4];
	struct kinfo_proc info;
	size_t size;

	// Initialize the flags so that, if sysctl fails for some bizarre
	// reason, we get a predictable result.

	info.kp_proc.p_flag = 0;

	// Initialize mib, which tells sysctl the info we want, in this case
	// we're looking for information about a specific process ID.

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PID;
	mib[3] = getpid();

	// Call sysctl.

	size = sizeof(info);
	junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
	assert(junk == 0);

	// We're being debugged if the P_TRACED flag is set.

	return ((info.kp_proc.p_flag & P_TRACED) != 0);
#else
	return false;
#endif
}

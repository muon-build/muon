/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <errno.h>
#include <stdio.h>
#include <windows.h>

#include "platform/os.h"

bool os_chdir(const char *path)
{
	BOOL res;

	res = SetCurrentDirectory(path);
	if (!res) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND) {
			errno = ENOENT;
		}
		else if (GetLastError() == ERROR_PATH_NOT_FOUND) {
			errno = ENOTDIR;
		}
		else if (GetLastError() == ERROR_FILENAME_EXCED_RANGE) {
			errno = ENAMETOOLONG;
		} else {
			errno = EIO;
		}
	}

	return res;
}

char *os_getcwd(char *buf, size_t size)
{
	DWORD len;

        /* set errno to ERANGE for crossplatform usage of getcwd() in path.c */
	len = GetCurrentDirectory(0UL, NULL);
	if (size < len) {
		errno = ERANGE;
		return NULL;
	}

	len = GetCurrentDirectory(size, buf);
	if (!len) {
		errno = EPERM;
		return NULL;
	}

	return buf;
}

/*
 * getopt ported from musl libc
 */
char *optarg;
int optind = 1, opterr = 1, optopt, __optpos, __optreset = 0;

#define optpos __optpos

int
os_getopt(int argc, char * const argv[], const char *optstring)
{
	int i;
	char c, d;

	if (!optind || __optreset) {
		__optreset = 0;
		__optpos = 0;
		optind = 1;
	}

	if (optind >= argc || !argv[optind]) {
		return -1;
	}

	if (argv[optind][0] != '-') {
		if (optstring[0] == '-') {
			optarg = argv[optind++];
			return 1;
		}
		return -1;
	}

	if (!argv[optind][1]) {
		return -1;
	}

	if (argv[optind][1] == '-' && !argv[optind][2]) {
		return optind++, -1;
	}

	if (!optpos) {
		optpos++;
	}

	c = argv[optind][optpos];
	++optpos;

	if (!argv[optind][optpos]) {
		optind++;
		optpos = 0;
	}

	if (optstring[0] == '-' || optstring[0] == '+') {
		optstring++;
	}

	i = 0;
	do {
		d = optstring[i];
		i++;
	} while (d != c);

	if (d != c || c == ':') {
		optopt = c;
		if (optstring[0] != ':' && opterr) {
			fprintf(stderr, "%s: unrecognized option: %c\n", argv[0], c);
		}
		return '?';
	}
	if (optstring[i] == ':') {
		optarg = 0;
		if (optstring[i + 1] != ':' || optpos) {
			optarg = argv[optind++] + optpos;
			optpos = 0;
		}
		if (optind > argc) {
			optopt = c;
			if (optstring[0] == ':') {
				return ':';
			}
			if (opterr) {
				fprintf(stderr, "%s: option requires an argument: %c\n", argv[0], c);
			}
			return '?';
		}
	}
	return c;
}

uint32_t
os_parallel_job_count(void)
{
	// TODO: this needs a real implementation
	return 4;
}

double
os_getloadavg(void)
{
	// TODO: this needs a real implementation
	return 0;
}

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

bool
os_chdir(const char *path)
{
	BOOL res;

	res = SetCurrentDirectory(path);
	if (!res) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND) {
			errno = ENOENT;
		}else if (GetLastError() == ERROR_PATH_NOT_FOUND) {
			errno = ENOTDIR;
		}else if (GetLastError() == ERROR_FILENAME_EXCED_RANGE) {
			errno = ENAMETOOLONG;
		} else {
			errno = EIO;
		}
	}

	return res;
}

char *
os_getcwd(char *buf, size_t size)
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

static uint32_t
count_bits(ULONG_PTR bit_mask)
{
	DWORD lshift;
	uint32_t bit_count = 0;
	ULONG_PTR bit_test;
	DWORD i;

	lshift = sizeof(ULONG_PTR) * 8 - 1;
	bit_test = (ULONG_PTR)1 << lshift;

	for (i = 0; i <= lshift; i++) {
		bit_count += ((bit_mask & bit_test) ? 1 : 0);
		bit_test /= 2;
	}

	return bit_count;
}

int32_t
os_ncpus(void)
{
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION iter;
	uint32_t ncpus;
	DWORD length;
	DWORD byte_offset;
	BOOL ret;

	buffer = NULL;
	length = 0UL;
	ret = GetLogicalProcessorInformation(buffer, &length);
	/*
	 * buffer and length values make this function failing
	 * with error being ERROR_INSUFFICIENT_BUFFER.
	 * Error not being ERROR_INSUFFICIENT_BUFFER is very unlikely.
	 */
	if (!ret) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			return -1;
		}
		/*
		 * Otherwise length is the size in bytes to allocate
		 */
	}

	buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(length);
	if (!buffer) {
		return -1;
	}

	ret = GetLogicalProcessorInformation(buffer, &length);
	/*
	 * Should not fail as buffer and length have the correct values,
	 * but anyway, we check the returned value.
	 */
	if (!ret) {
		free(buffer);
		return -1;
	}

	iter = buffer;
	byte_offset = 0;
	ncpus = 0;

	while (byte_offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= length) {
		switch (iter->Relationship) {
		case RelationProcessorCore:
			ncpus += count_bits(iter->ProcessorMask);
			break;
		default:
			break;
		}
		byte_offset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		iter++;
	}

	free(buffer);

	return ncpus;
}

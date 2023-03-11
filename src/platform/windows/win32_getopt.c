/*
 * Ported from musl libc
 * SPDX-FileCopyrightText: Copyright © 2005-2020 Rich Felker, et al.
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"

#include <stdio.h>

#include "platform/windows/win32_getopt.h"

char *optarg;
int optind = 1, opterr = 1, optopt, __optpos, __optreset = 0;

#define optpos __optpos

int
getopt(int argc, char * const argv[], const char *optstring)
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

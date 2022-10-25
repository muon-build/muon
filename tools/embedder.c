/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static bool
embed(const char *path, const char *basename)
{
	int c;
	uint32_t i = 0;
	FILE *f;

	if (!(f = fopen(path, "rb"))) {
		fprintf(stderr, "couldn't open '%s' for reading\n", path);
		return false;
	}

	printf("{ .name = \"%s\", .src = (char []){\n", basename);

	while ((c = fgetc(f)) != EOF) {
		printf("0x%x, ", c);

		if ((i % 14) == 0) {
			fputs("\n", stdout);
		}
		++i;
	}

	fputs("0x0\n} },\n", stdout);

	return fclose(f) == 0;
}

int
main(int argc, char *const argv[])
{
	assert(argc >= 1);
	assert(((argc - 1) & 1) == 0 && "you must pass an even number of arguments");

	printf("uint32_t embedded_len = %d;\n"
		"\n"
		"static struct embedded_file embedded[] = {\n"
		,
		(argc - 1) / 2);

	uint32_t i;
	for (i = 1; i < (uint32_t)argc; i += 2) {
		if (!embed(argv[i], argv[i + 1])) {
			return 1;
		}
	}

	printf("};\n");
}

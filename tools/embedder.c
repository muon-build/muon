/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifdef _MSC_VER
#define _CRT_DECLARE_NONSTDC_NAMES 0
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _MSC_VER
#include <io.h>
#define access _access
#ifndef F_OK
#define F_OK 0
#endif
#else
#include <unistd.h>
#endif

static bool
embed(const char *path, const char *embedded_name)
{
#ifndef MUON_HAVE_EMBED
	int c;
	uint32_t i = 0;
	FILE *f;

	if (!(f = fopen(path, "rb"))) {
		fprintf(stderr, "couldn't open '%s' for reading\n", path);
		return false;
	}
#else
	if (access(path, F_OK)) {
		fprintf(stderr, "'%s' doesn't exist\n", path);
		return false;
	}
#endif

	printf("{ .name = \"%s\", .src = { .label = \"%s\", .type = source_type_embedded, .src = (char []){\n", embedded_name, embedded_name);

#ifndef MUON_HAVE_EMBED
	while ((c = fgetc(f)) != EOF) {
		// output signed char
		printf("%hhd, ", (signed char)c);

		if ((i % 14) == 0) {
			fputs("\n", stdout);
		}
		++i;
	}

	fprintf(stdout, "0x0\n}, .len = %d } },\n", i);

	return fclose(f) == 0;
#else
	fprintf(stdout, "#embed \"%s\"\n, 0x0\n}, .len = sizeof((char []){\n#embed \"%s\"\n}) } },\n", path, path);
	return true;
#endif
}

int
main(int argc, char *const argv[])
{
	assert(argc >= 1);
	assert(((argc - 1) & 1) == 0 && "you must pass an even number of arguments");

	printf("uint32_t embedded_len = %d;\n"
	       "\n"
	       "static struct embedded_file embedded[] = {\n",
		(argc - 1) / 2);

	uint32_t i;
	for (i = 1; i < (uint32_t)argc; i += 2) {
		if (!embed(argv[i], argv[i + 1])) {
			return 1;
		}
	}

	printf("};\n");
}

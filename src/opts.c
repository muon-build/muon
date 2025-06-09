/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdbool.h>
#include <string.h>

#include "log.h"
#include "opts.h"
#include "platform/assert.h"

bool
check_operands(uint32_t argc, uint32_t argi, int32_t expected)
{
	assert(argc >= argi);

	uint32_t rem = argc - argi;

	if (expected < 0) {
		return true;
	}

	if (rem < (uint32_t)expected) {
		LOG_E("missing operand");
		return false;
	} else if (rem > (uint32_t)expected) {
		LOG_E("too many operands, got %d but expected %d (did you try passing options after operands?)", rem, expected);
		return false;
	}

	return true;
}

void
print_usage(FILE *f, const struct command *commands, const char *pre, const char *opts, const char *post)
{
	uint32_t i;
	fprintf(f, "usage: %s%s%s%s\n", pre, opts ? " [options]" : "", commands ? " [command]" : "", post ? post : "");

	if (opts) {
		fprintf(f,
			"options:\n"
			"%s"
			"  -h - show this message\n",
			opts);
	}

	if (commands) {
		fprintf(f, "commands:\n");

		for (i = 0; commands[i].name; ++i) {
			if (commands[i].desc) {
				fprintf(f, "  %-12s", commands[i].name);
				fprintf(f, "- %s", commands[i].desc);
				fputc('\n', f);
			}
		}
	}
}

bool
find_cmd(const struct command *commands, uint32_t *ret, uint32_t argc, uint32_t argi, char *const argv[], bool optional)
{
	uint32_t i;
	const char *cmd;

	if (argi >= argc) {
		if (optional) {
			return true;
		} else {
			LOG_E("missing command");
			return false;
		}
	} else {
		cmd = argv[argi];
	}

	for (i = 0; commands[i].name; ++i) {
		if (strcmp(commands[i].name, cmd) == 0) {
			*ret = i;
			return true;
		}
	}

	LOG_E("invalid command '%s'", cmd);
	return false;
}

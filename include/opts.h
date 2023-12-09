/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_OPTS_H
#define MUON_OPTS_H

#include "lang/workspace.h"
#include "platform/os.h"

/* OPTSTART should be pretty self-explanatory.  You just pass it the optstring
 * that you would pass to getopt().  "h" is added to this optstring for you.
 *
 * OPTEND is a little bit more involved, the first 4 arguments are used to
 * construct the help message, while the 5th argument should be the number of
 * required operands for this subcommand, or -1 which disables the check.
 */
#define OPTSTART(optstring) \
	signed char opt; \
	optind = 1; \
	while ((opt = os_getopt(argc - argi, &argv[argi], optstring "h")) != -1) { \
		switch (opt) {
#define OPTEND(usage_pre, usage_post, usage_opts, commands, operands) \
	case 'h': \
		print_usage(stdout, commands, usage_pre, usage_opts, usage_post); \
		exit(0); \
		break; \
	default: \
		print_usage(stderr, commands, usage_pre, usage_opts, usage_post); \
		return false; \
	} \
	} \
	if (!check_operands(argc, (argi + optind), operands)) { \
		print_usage(stderr, commands, usage_pre, usage_opts, usage_post); \
		return false; \
	} \
	argi += optind;

typedef bool (*cmd_func)(uint32_t argc, uint32_t argi, char *const[]);

struct command {
	const char *name;
	cmd_func cmd;
	const char *desc;
};

void print_usage(FILE *f, const struct command *commands,
	const char *pre, const char *opts, const char *post);
bool find_cmd(const struct command *commands, cmd_func *ret,
	uint32_t argc, uint32_t argi, char *const argv[], bool optional);
bool check_operands(uint32_t argc, uint32_t argi, int32_t expected);
#endif

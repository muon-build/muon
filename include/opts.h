#ifndef MUON_OPTS_H
#define MUON_OPTS_H

#include <stdio.h>

#include "lang/workspace.h"

#define OPTSTART(optstring) \
	signed char opt; \
	optind = 1; \
	while ((opt = getopt(argc - argi, &argv[argi], optstring "h")) != -1) { \
		switch (opt) {
#define OPTEND(usage_pre, usage_post, usage_opts, commands) \
case 'h': \
	print_usage(stdout, commands, usage_pre, usage_opts, usage_post); \
	exit(0); \
	break; \
default: \
	print_usage(stderr, commands, usage_pre, usage_opts, usage_post); \
	return false; \
} \
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
bool parse_config_key_value(struct workspace *wk, char *lhs, const char *val);
bool parse_config_opt(struct workspace *wk, char *lhs);
#endif

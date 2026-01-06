/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_OPTS_H
#define MUON_OPTS_H

#include "datastructures/arr.h"
#include "lang/types.h"

struct workspace;
typedef bool (*opt_cmd_func)(struct workspace *wk, uint32_t argc, uint32_t argi, char *const[]);

struct opt_command {
	const char *name;
	opt_cmd_func cmd;
	const char *desc;
	const char *desc_long;
	bool skip_gather;
};

struct opt_gather_all_ctx {
	struct arr commands;
	const struct opt_command *command;
	obj trace;
	bool enabled;
};
extern struct opt_gather_all_ctx opt_gather_all_ctx;

struct opt_gathered_command {
	struct arr opts;
	const char *desc, *desc_long, *usage_post;
	obj trace;
	const struct opt_command *commands;
};

struct opt_ctx {
	char *const *argv;
	const uint32_t argc;
	const uint32_t original_argi;
	uint32_t *argi;

	int32_t n_operands;
	const struct opt_command *commands;
	const char *usage_post;
	void (*extra_help)(struct workspace *wk);

	struct arr table;
	uint32_t argpos;
	char c;
	bool match, initialized, gathering;
	char *optarg;
	uint32_t optarg_enum_value;
};

struct opt_match_opts {
	char c;
	const char *desc;
	const char *value_name;
	const char *desc_long;
	const struct opt_match_enum_table *enum_table;
	uint32_t enum_table_len;
};

#define opt_match_enum_table(table) .enum_table = (table), .enum_table_len = ARRAY_LEN(table)

struct opt_match_enum_table {
	const char *long_name;
	uint32_t val;
	const char *short_name;
};

#define opt_for(...)                                                        \
	struct opt_ctx opt_ctx = { argv, argc, argi, &argi, __VA_ARGS__ }; \
	while (opt_get_next(wk, &opt_ctx))
bool opt_get_next(struct workspace *wk, struct opt_ctx *ctx);

#define opt_end()                       \
	if (opt_gather_all_ctx.enabled) \
		return false

bool opt_match_(struct workspace *wk, struct opt_ctx *ctx, const struct opt_match_opts *opt);
#define opt_match(...)                                                        \
	opt_match_(wk, &opt_ctx, &(struct opt_match_opts){ __VA_ARGS__ }) \

void opt_gather_all(struct workspace *wk, opt_cmd_func root);
void opt_gather_all_push_custom(struct workspace *wk, const struct opt_gathered_command *cmd);

bool opt_find_cmd(const struct opt_command *commands, uint32_t *ret, uint32_t argc, uint32_t argi, char *const argv[], bool optional);
bool opt_check_operands(uint32_t argc, uint32_t argi, int32_t expected);
#endif

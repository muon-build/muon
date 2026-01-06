/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lang/workspace.h"
#include "log.h"
#include "opts.h"
#include "platform/assert.h"

struct opt_gather_all_ctx opt_gather_all_ctx = { 0 };

bool
opt_check_operands(uint32_t argc, uint32_t argi, int32_t expected)
{
	assert(argc >= argi);

	if (expected < 0) {
		return true;
	}

	uint32_t rem = argc - argi;

	if (rem < (uint32_t)expected) {
		LOG_E("missing operand");
		return false;
	} else if (rem > (uint32_t)expected) {
		LOG_E("too many operands, got %d but expected %d (did you try passing options after operands?)", rem, expected);
		return false;
	}

	return true;
}

bool
opt_find_cmd(const struct opt_command *commands, uint32_t *ret, uint32_t argc, uint32_t argi, char *const argv[], bool optional)
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

//------------------------------------------------------------------------------

static void
opt_print_usage(struct workspace *wk, struct opt_ctx *ctx, FILE *f)
{
	uint32_t i;
	fprintf(f, "usage: %s [options]%s%s\n", ctx->argv[ctx->original_argi], ctx->commands ? " [command]" : "", ctx->usage_post ? ctx->usage_post : "");

	fprintf(f, "options:\n");
	for (i = 0; i < ctx->table.len; ++i) {
		const struct opt_match_opts *opt = arr_get(&ctx->table, i);
		fprintf(f, " -%c", opt->c);

		if (opt->enum_table_len) {
			fprintf(f, " <%s", opt->enum_table[0].long_name);
			for (uint32_t i = 1; i < opt->enum_table_len; ++i) {
				fprintf(f, "|%s", opt->enum_table[i].long_name);
			}
			fprintf(f, ">");
		} else if (opt->value_name) {
			fprintf(f, " <%s>", opt->value_name);
		}

		fprintf(f, " - %s\n", opt->desc);
	}

	if (ctx->commands) {
		fprintf(f, "commands:\n");

		for (i = 0; ctx->commands[i].name; ++i) {
			if (ctx->commands[i].desc) {
				fprintf(f, "  %-12s", ctx->commands[i].name);
				fprintf(f, "- %s", ctx->commands[i].desc);
				fputc('\n', f);
			}
		}
	}

	if (ctx->extra_help) {
		ctx->extra_help(wk);
	}
}

static void MUON_NORETURN
opt_error(struct opt_ctx *ctx)
{
	// LOG_N("get help with -h");
	exit(1);
}

bool
opt_match_(struct workspace *wk, struct opt_ctx *ctx, const struct opt_match_opts *opt)
{
	if (ctx->gathering) {
		arr_push(wk->a, &ctx->table, opt);
		return false;
	}

	if (ctx->match) {
		return false;
	}

	if (opt->c == ctx->c) {
		ctx->match = true;
		ctx->optarg = 0;
		ctx->optarg_enum_value = 0;
		if (opt->value_name || opt->enum_table_len) {
			if (ctx->argpos) {
				ctx->optarg = ctx->argv[*ctx->argi] + ctx->argpos;
				ctx->argpos = 0;
				++(*ctx->argi);
			} else if (*ctx->argi < ctx->argc) {
				ctx->optarg = ctx->argv[*ctx->argi];
				++(*ctx->argi);
			} else {
				LOG_E("option requires an argument: %c", opt->c);
				opt_error(ctx);
			}

			if (opt->enum_table_len) {
				uint32_t i;
				for (i = 0; i < opt->enum_table_len; ++i) {
					if ((opt->enum_table[i].short_name
						    && strcmp(ctx->optarg, opt->enum_table[i].short_name) == 0)
						|| strcmp(ctx->optarg, opt->enum_table[i].long_name) == 0) {
						ctx->optarg_enum_value = opt->enum_table[i].val;
						break;
					}
				}

				if (i == opt->enum_table_len) {
					LOG_E("invalid value %s for option %c", ctx->optarg, opt->c);
					LOG_I("supported values:");
					for (i = 0; i < opt->enum_table_len; ++i) {
						LOG_I("  - %s | %s",
							opt->enum_table[i].short_name,
							opt->enum_table[i].long_name);
					}
					opt_error(ctx);
				}
			}
		}
		return true;
	}

	return false;
}

static void
opt_gather_begin(struct workspace *wk, struct opt_ctx *ctx)
{
	arr_init(wk->a, &ctx->table, 16, struct opt_match_opts);
	arr_push(wk->a, &ctx->table, &(struct opt_match_opts){ 'h', "show help" });
	ctx->gathering = true;
}

static int32_t opt_gathered_command_sort(const void *_a, const void *_b, void *ctx)
{
	struct workspace *wk = ctx;
	const struct opt_gathered_command *a = _a, *b = _b;
	obj a_name, b_name;
	obj_array_join(wk, false, a->trace, make_str(wk, " "), &a_name);
	obj_array_join(wk, false, b->trace, make_str(wk, " "), &b_name);
	return strcmp(get_str(wk, a_name)->s, get_str(wk, b_name)->s);
}

void
opt_gather_all(struct workspace *wk, opt_cmd_func root)
{
	struct opt_gather_all_ctx *ga = &opt_gather_all_ctx;

	ga->enabled = true;
	arr_init(wk->a, &ga->commands, 16, struct opt_gathered_command);
	ga->trace = make_obj(wk, obj_array);

	root(wk, 0, 0, 0);

	ga->enabled = false;

	arr_sort(&ga->commands, wk, opt_gathered_command_sort);
}

void
opt_gather_all_push_custom(struct workspace *wk, const struct opt_gathered_command *cmd)
{
	struct opt_gather_all_ctx *ga = &opt_gather_all_ctx;
	uint32_t i = arr_push(wk->a, &ga->commands, cmd);
	struct opt_gathered_command *pushed = arr_get(&ga->commands, i);
	obj_array_dup(wk, ga->trace, &pushed->trace);
}

static void
opt_gather_all_push(struct workspace *wk, struct opt_ctx *ctx)
{
	struct opt_gather_all_ctx *ga = &opt_gather_all_ctx;
	struct opt_gathered_command cmd = { 0 };
	if (ga->command) {
		cmd.desc = ga->command->desc;
		cmd.desc_long = ga->command->desc_long;
	}
	if (ctx) {
		cmd.opts = ctx->table;
		cmd.usage_post = ctx->usage_post;
		cmd.commands = ctx->commands;
	}
	opt_gather_all_push_custom(wk, &cmd);
}

static int32_t opt_table_sort(const void *_a, const void *_b, void *ctx)
{
	const struct opt_match_opts *a = _a, *b = _b;
	if (a->c == 'h') {
		return 1;
	} else if (b->c == 'h') {
		return -1;
	} else {
		char a_l = str_char_to_lower(a->c), b_l = str_char_to_lower(b->c);
		if (a_l == b_l) {
			return b->c - a->c;
		} else {
			return a_l - b_l;
		}
	}
}

static int opt_commands_sort(const void *_a, const void *_b)
{
	const struct opt_command *a = _a, *b = _b;
	return strcmp(a->name, b->name);
}

static void
opt_finished(struct workspace *wk, struct opt_ctx *ctx)
{
	if (!opt_check_operands(ctx->argc, *ctx->argi, ctx->n_operands)) {
		opt_error(ctx);
	}
}

bool
opt_get_next(struct workspace *wk, struct opt_ctx *ctx)
{
	struct opt_gather_all_ctx *ga = &opt_gather_all_ctx;

	if (!ctx->initialized) {
		++(*ctx->argi);
		if (ctx->commands) {
			uint32_t len;
			for (len = 0; ctx->commands[len].name; ++len) {
			}
			qsort((void *)ctx->commands, len, sizeof(struct opt_command), opt_commands_sort);
		}

		ctx->initialized = true;
		if (ga->enabled) {
			opt_gather_begin(wk, ctx);
			return true;
		}
	} else if (ctx->gathering) {
		arr_sort(&ctx->table, wk, opt_table_sort);

		if (ga->enabled) {
			opt_gather_all_push(wk, ctx);

			if (ctx->commands) {
				for (uint32_t i = 0; ctx->commands[i].name; ++i) {
					if (!ctx->commands[i].desc || ctx->commands[i].skip_gather) {
						continue;
					}

					obj_array_push(
						wk, ga->trace, make_str(wk, ctx->commands[i].name));
					ga->command = &ctx->commands[i];
					if (ctx->commands[i].cmd) {
						ctx->commands[i].cmd(wk, 0, 0, 0);
					} else {
						opt_gather_all_push(wk, 0);
					}
					obj_array_pop(wk, ga->trace);
				}
			}
			return false;
		} else {
			opt_print_usage(wk, ctx, stdout);
			exit(0);
		}
	} else {
		if (ctx->c == 'h') {
			opt_gather_begin(wk, ctx);
			return true;
		}

		if (!ctx->match) {
			LOG_E("unknown option: %c", ctx->c);
			opt_error(ctx);
		}
	}

	if (*ctx->argi >= ctx->argc) {
		opt_finished(wk, ctx);
		return false;
	}

	const char *cur = ctx->argv[*ctx->argi];
	if (cur[0] != '-') {
		opt_finished(wk, ctx);
		return false;
	} else if (!cur[1]) {
		opt_finished(wk, ctx);
		return false;
	} else if (cur[1] == '-' && !cur[2]) {
		++(*ctx->argi);
		opt_finished(wk, ctx);
		return false;
	}

	if (!ctx->argpos) {
		ctx->argpos = 1;
	}

	ctx->c = cur[ctx->argpos];
	++ctx->argpos;

	if (!cur[ctx->argpos]) {
		++(*ctx->argi);
		ctx->argpos = 0;
	}
	ctx->match = false;
	return true;
}

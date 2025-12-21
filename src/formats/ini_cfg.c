/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "error.h"
#include "formats/ini.h"
#include "formats/ini_cfg.h"
#include "lang/string.h"
#include "platform/assert.h"

struct ini_cfg_ctx {
	struct workspace *wk;
	const struct ini_cfg_key *keys;
	uint32_t keys_len;
	void *usr_ctx;
	void *dest;
};

static bool
ini_cfg_parse_cb(void *_ctx,
	struct source *src,
	const char *sect,
	const char *k,
	const char *v,
	struct source_location location)
{
	struct ini_cfg_ctx *ctx = _ctx;
	struct workspace *wk = ctx->wk;

	if (!k || !*k) {
		error_messagef(wk, src, location, log_error, "missing key");
		return false;
	} else if (!v || !*v) {
		error_messagef(wk, src, location, log_error, "missing value");
		return false;
	} else if (sect) {
		error_messagef(wk, src, location, log_error, "invalid section");
		return false;
	}

	uint32_t i;
	for (i = 0; ctx->keys[i].name; ++i) {
		if (strcmp(k, ctx->keys[i].name) != 0) {
			continue;
		}

		void *val_dest = (((uint8_t *)(ctx->dest)) + ctx->keys[i].off);

		if (ctx->keys[i].deprecated) {
			error_messagef(wk, src, location, log_warn, "option %s is deprecated", ctx->keys[i].name);
		}

		switch (ctx->keys[i].type) {
		case ini_cfg_type_uint: {
			int64_t lval;
			if (!str_to_i(&STRL(v), &lval, true)) {
				error_messagef(wk, src, location, log_error, "unable to parse integer");
				return false;
			} else if (lval < 0 || lval > UINT32_MAX) {
				error_messagef(
					wk, src, location, log_error, "integer outside of range 0-%u", UINT32_MAX);
				return false;
			}

			uint32_t val = lval;

			if (ctx->keys[i].deprecated_action) {
				ctx->keys[i].deprecated_action(ctx, &val);
			} else {
				memcpy(val_dest, &val, sizeof(uint32_t));
			}
			break;
		}
		case ini_cfg_type_str: {
			char *start, *end;
			start = strchr(v, '\'');
			end = strrchr(v, '\'');

			if (!start || !end || start == end) {
				error_messagef(wk, src, location, log_error, "expected single-quoted string");
				return false;
			}

			*end = 0;
			++start;

			if (ctx->keys[i].deprecated_action) {
				ctx->keys[i].deprecated_action(ctx, &start);
			} else {
				memcpy(val_dest, &start, sizeof(char *));
			}
			break;
		}
		case ini_cfg_type_bool: {
			bool val;
			if (strcmp(v, "true") == 0) {
				val = true;
			} else if (strcmp(v, "false") == 0) {
				val = false;
			} else {
				error_messagef(
					wk, src, location, log_error, "invalid value for bool, expected true/false");
				return false;
			}

			if (ctx->keys[i].deprecated_action) {
				ctx->keys[i].deprecated_action(ctx, &val);
			} else {
				memcpy(val_dest, &val, sizeof(bool));
			}
			break;
		}
		case ini_cfg_type_enum: {
			assert(ctx->keys[i].enum_tbl);

			uint32_t j, val = 0;
			for (j = 0; ctx->keys[i].enum_tbl[j].name; ++j) {
				if (strcmp(v, ctx->keys[i].enum_tbl[j].name) == 0) {
					val = ctx->keys[i].enum_tbl[j].val;
					break;
				}
			}

			if (!ctx->keys[i].enum_tbl[j].name) {
				error_messagef(
					wk, src, location, log_error, "invalid value for %s: %s", ctx->keys[i].name, v);
				return false;
			}

			if (ctx->keys[i].deprecated_action) {
				ctx->keys[i].deprecated_action(ctx, &val);
			} else {
				memcpy(val_dest, &val, sizeof(uint32_t));
			}
		}
		}

		break;
	}

	if (!ctx->keys[i].name) {
		error_messagef(wk, src, location, log_error, "unknown config key: %s", k);
		return false;
	}

	return true;
}

bool
ini_cfg_parse(struct workspace *wk,
	const char *path,
	const struct ini_cfg_key *keys,
	void *usr_ctx,
	void *dest)
{
	struct ini_cfg_ctx ctx = {
		.wk = wk,
		.keys = keys,
		.usr_ctx = usr_ctx,
		.dest = dest,
	};

	char *cfg_buf = NULL;
	struct source cfg_src = { 0 };
	return ini_parse(wk, path, &cfg_src, &cfg_buf, ini_cfg_parse_cb, &ctx);
}

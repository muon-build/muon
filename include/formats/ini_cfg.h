/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_INI_CFG_H
#define MUON_FORMATS_INI_CFG_H

#include <stdint.h>
#include <stdbool.h>

struct workspace;

enum ini_cfg_type {
	ini_cfg_type_uint,
	ini_cfg_type_str,
	ini_cfg_type_bool,
	ini_cfg_type_enum,
};

struct ini_cfg_enum {
	const char *name;
	uint32_t val;
};

struct ini_cfg_key {
	const char *name;
	enum ini_cfg_type type;
	uint32_t off;
	bool deprecated;
	void((*deprecated_action)(void *ctx, void *val));
	struct ini_cfg_enum *enum_tbl;
};

bool
ini_cfg_parse(struct workspace *wk,
	const char *path,
	const struct ini_cfg_key *keys,
	void *usr_ctx,
	void *dest);
#endif

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FORMATS_TAP_H
#define MUON_FORMATS_TAP_H

#include <stdbool.h>
#include <stdint.h>

struct tap_parse_result {
	uint32_t total, pass, fail, skip;
	bool all_ok;
};

void tap_parse(char *buf, uint64_t buf_len, struct tap_parse_result *res);
#endif

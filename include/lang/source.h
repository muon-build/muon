#ifndef MUON_LANG_SOURCE_H
/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define MUON_LANG_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

enum source_type {
	source_type_unknown,
	source_type_file,
	source_type_embedded,
};

struct source {
	const char *label;
	const char *src;
	uint64_t len;
	enum source_type type;
	bool is_weak_reference;
};

struct source_location {
	uint32_t off, len;
};

#endif

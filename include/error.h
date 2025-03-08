/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_ERROR_H
#define MUON_ERROR_H

#include "compat.h"

#include "lang/types.h"
#include "log.h"
#include "platform/filesystem.h"

#define UNREACHABLE assert(false && "unreachable")
#define UNREACHABLE_RETURN                      \
	do {                                    \
		assert(false && "unreachable"); \
		return 0;                       \
	} while (0)

enum error_diagnostic_store_replay_opts {
	error_diagnostic_store_replay_errors_only = 1 << 0,
	error_diagnostic_store_replay_dont_include_sources = 1 << 1,
	error_diagnostic_store_replay_werror = 1 << 2,
	error_diagnostic_store_replay_prepare_only = 1 << 3,
};

struct error_diagnostic_message {
	struct source_location location;
	enum log_level lvl;
	const char *msg;
	uint32_t src_idx;
};

void error_unrecoverable(const char *fmt, ...) MUON_ATTR_FORMAT(printf, 1, 2);
void error_message(const struct source *src, struct source_location location, enum log_level lvl, enum error_message_flag flags, const char *msg);
void error_message_flush_coalesced_message(void);
void
error_messagev(const struct source *src, struct source_location location, enum log_level lvl, const char *fmt, va_list args);
void error_messagef(const struct source *src, struct source_location location, enum log_level lvl, const char *fmt, ...)
	MUON_ATTR_FORMAT(printf, 4, 5);

void error_diagnostic_store_init(struct workspace *wk);
void error_diagnostic_store_destroy(struct workspace *wk);
struct arr *error_diagnostic_store_get(void);
bool error_diagnostic_store_replay(struct workspace *wk, enum error_diagnostic_store_replay_opts opts);
void
error_diagnostic_store_push(uint32_t src_idx, struct source_location location, enum log_level lvl, const char *msg);
void list_line_range(const struct source *src, struct source_location location, uint32_t context);

void reopen_source(const struct source *src, struct source *src_reopened, bool *destroy_source);

struct detailed_source_location {
	struct source_location loc;
	uint32_t line, col, start_of_line, end_line, end_col;
};

enum get_detailed_source_location_flag {
	get_detailed_source_location_flag_multiline = 1 << 0,
};

void get_detailed_source_location(const struct source *src,
	struct source_location loc,
	struct detailed_source_location *dloc,
	enum get_detailed_source_location_flag flags);
#endif

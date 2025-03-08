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
};

void error_unrecoverable(const char *fmt, ...) MUON_ATTR_FORMAT(printf, 1, 2);
void error_message(const struct source *src, struct source_location location, enum log_level lvl, enum error_message_flag flags, const char *msg);
void error_message_flush_coalesced_message(void);
void
error_messagev(const struct source *src, struct source_location location, enum log_level lvl, const char *fmt, va_list args);
void error_messagef(const struct source *src, struct source_location location, enum log_level lvl, const char *fmt, ...)
	MUON_ATTR_FORMAT(printf, 4, 5);

void error_diagnostic_store_init(struct workspace *wk);
void error_diagnostic_store_replay(enum error_diagnostic_store_replay_opts opts, bool *saw_error);
void
error_diagnostic_store_push(uint32_t src_idx, struct source_location location, enum log_level lvl, const char *msg);
void error_diagnostic_store_redirect(struct source *src, struct source_location location);
void error_diagnostic_store_redirect_reset(void);
void list_line_range(const struct source *src, struct source_location location, uint32_t context);

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

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_EXTERNAL_LIBCURL_H
#define MUON_EXTERNAL_LIBCURL_H

#include <stdbool.h>
#include <stdint.h>

extern const bool have_libcurl;

enum mc_fetch_collect_result {
	mc_fetch_collect_result_pending,
	mc_fetch_collect_result_done,
	mc_fetch_collect_result_error,
};

enum mc_fetch_flag {
	mc_fetch_flag_verbose = 1 << 0,
};

void mc_init(void);
void mc_deinit(void);
int32_t mc_fetch_begin(const char *url, uint8_t **buf, uint64_t *len, enum mc_fetch_flag flags);
enum mc_fetch_collect_result mc_fetch_collect(int32_t i);
bool mc_wait(void);
#endif

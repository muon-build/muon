/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "external/libcurl.h"
#include "log.h"

const bool have_libcurl = false;

void
mc_init(void)
{
}

void
mc_deinit(void)
{
}

int32_t
mc_fetch_begin(const char *url, uint8_t **buf, uint64_t *len, enum mc_fetch_flag flags)
{
	LOG_W("libcurl not enabled");
	return -1;
}

enum mc_fetch_collect_result
mc_fetch_collect(int32_t i)
{
	LOG_W("libcurl not enabled");
	return mc_fetch_collect_result_error;
}

bool
mc_wait(void)
{
	LOG_W("libcurl not enabled");
	return false;
}

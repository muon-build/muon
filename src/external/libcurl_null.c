/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "external/libcurl.h"
#include "log.h"

const bool have_libcurl = false;

void
muon_curl_init(void)
{
}

void
muon_curl_deinit(void)
{
}

bool
muon_curl_fetch(const char *url, uint8_t **buf, uint64_t *len)
{
	LOG_W("libcurl not enabled");
	return false;
}

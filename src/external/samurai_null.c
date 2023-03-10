/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "external/samurai.h"
#include "log.h"

const bool have_samurai = false;

bool
muon_samu(uint32_t argc, char *const argv[])
{
	LOG_W("samurai not enabled");
	return false;
}

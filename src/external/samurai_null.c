/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "external/samurai.h"
#include "log.h"

const bool have_samurai = false;

bool
samu_main(int argc, char *argv[], struct samu_opts *opts)
{
	LOG_W("samurai not enabled");
	return false;
}

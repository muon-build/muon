/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <samu.h>

#include "buf_size.h"
#include "external/samurai.h"
#include "platform/filesystem.h"
#include "platform/path.h"

const bool have_samurai = true;

bool
muon_samu(uint32_t argc, char *const argv[])
{
	return samu_main(argc, (char **)argv) == 0;
}

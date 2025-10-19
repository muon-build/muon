/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_VSENV_H
#define MUON_VSENV_H
#include <stdbool.h>
#include "lang/types.h"
struct workspace;
enum setup_platform_env_requirement {
	setup_platform_env_requirement_skip,
	setup_platform_env_requirement_required,
	setup_platform_env_requirement_auto,
	setup_platform_env_requirement_from_cache,
};

void setup_platform_env(struct workspace *wk, const char *build_dir, enum setup_platform_env_requirement req);
#endif

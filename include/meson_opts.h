/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_MESON_OPTS_H
#define MUON_MESON_OPTS_H

#include "lang/workspace.h"

bool translate_meson_opts(struct workspace *wk,
	uint32_t argc,
	uint32_t argi,
	char *argv[],
	uint32_t *new_argc,
	uint32_t *new_argi,
	char **new_argv[]);

#endif

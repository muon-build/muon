/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_CMD_INSTALL_H
#define MUON_CMD_INSTALL_H
#include <stdbool.h>

struct install_options {
	const char *destdir;
	bool dry_run;
};

bool install_run(struct install_options *opts);
#endif

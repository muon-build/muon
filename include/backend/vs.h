/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_VS_H
#define MUON_BACKEND_VS_H

#include "lang/workspace.h"

/* Debug and Release */
extern char *vs_configurations[2];

/* x64 x86, maybe later arm */
extern char *vs_platforms[2];

/* x64 win32, maybe later arm */
extern char *vs_machines[2];

struct vs_ctx {
	FILE *out;
	const struct project *project;
	obj tgt_id; /* current target */
	uint32_t vs_version;
	uint32_t idx;
};

void vs_get_project_filename(struct workspace *wk, struct sbuf *sb, struct obj_build_target *target);

bool vs_write_all(struct workspace *wk, enum backend_output backend);

#endif

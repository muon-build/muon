/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_BACKEND_H
#define MUON_BACKEND_BACKEND_H
#include "lang/workspace.h"

enum backend_output {
	backend_output_ninja,
	backend_output_vs,
	backend_output_vs2019,
	backend_output_vs2022,
};

bool backend_output(struct workspace *wk, enum backend_output);
#endif

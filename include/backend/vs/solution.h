/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_VS_SOLUTION_H
#define MUON_BACKEND_VS_SOLUTION_H

#include "compat.h"

#include "lang/workspace.h"

bool vs_write_solution(struct workspace *wk, void *ctx, FILE *out);

#endif

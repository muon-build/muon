/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BACKEND_VS_FILTER_H
#define MUON_BACKEND_VS_FILTER_H

#include "compat.h"

#include "lang/workspace.h"

bool vs_write_filter(struct workspace *wk, void *_ctx, FILE *out);

#endif

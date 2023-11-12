/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Seedo Paul <seedoeldhopaul@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "external/tinyjson.h"
#include "lang/types.h"
#include "lang/workspace.h"
#include "log.h"

bool
muon_json_to_dict(struct workspace *wk, char *json_str, obj *res)
{
	LOG_E("tinyjson not enabled");
	return false;
}

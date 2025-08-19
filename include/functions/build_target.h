/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_BUILD_TARGET_H
#define MUON_FUNCTIONS_BUILD_TARGET_H
#include "lang/func_lookup.h"

bool tgt_src_to_object_path(struct workspace *wk,
	const struct obj_build_target *tgt,
	enum compiler_language lang,
	obj src_file,
	bool relative,
	struct tstr *res);
bool tgt_src_to_pch_path(struct workspace *wk,
	const struct obj_build_target *tgt,
	enum compiler_language lang,
	obj src_file,
	struct tstr *res);

bool build_target_extract_all_objects(struct workspace *wk, uint32_t ip, obj self, obj *res, bool recursive);

FUNC_REGISTER(build_target);
#endif

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_ENVIRONMENT_H
#define MUON_FUNCTIONS_ENVIRONMENT_H
#include "lang/func_lookup.h"

enum environment_set_mode {
	environment_set_mode_set,
	environment_set_mode_append,
	environment_set_mode_prepend,
};

enum make_obj_environment_flag {
	make_obj_environment_flag_no_default_vars = 1 << 0,
	make_obj_environment_flag_set_subdir = 1 << 1,
};

obj make_obj_environment(struct workspace *wk, enum make_obj_environment_flag flags);
void environment_extend(struct workspace *wk, obj env, obj other);
bool environment_set(struct workspace *wk, obj env, enum environment_set_mode mode, obj key, obj vals, obj sep);
bool environment_to_dict(struct workspace *wk, obj env, obj *res);
void set_default_environment_vars(struct workspace *wk, obj env, bool set_subdir);
extern const struct func_impl impl_tbl_environment[];
#endif

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_ENVIRONMENT_H
#define MUON_FUNCTIONS_ENVIRONMENT_H
#include "functions/common.h"

enum environment_set_mode {
	environment_set_mode_set,
	environment_set_mode_append,
	environment_set_mode_prepend,
};

bool environment_set(struct workspace *wk, obj env, enum environment_set_mode mode, obj key, obj vals, obj sep);
bool environment_to_dict(struct workspace *wk, obj env, obj *res);
void set_default_environment_vars(struct workspace *wk, obj env, bool set_subdir);
extern const struct func_impl impl_tbl_environment[];
#endif

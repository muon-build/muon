/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_COERCE_H
#define MUON_COERCE_H
#include "lang/workspace.h"

enum requirement_type {
	requirement_skip,
	requirement_required,
	requirement_auto,
};

bool coerce_environment_from_kwarg(struct workspace *wk, struct args_kw *kw, bool set_subdir, obj *res);
bool coerce_key_value_dict(struct workspace *wk, uint32_t err_node, obj val, obj *res);
bool coerce_include_type(struct workspace *wk, const struct str *str, uint32_t err_node, enum include_type *res);
bool coerce_string_to_file(struct workspace *wk, const char *dir, obj string, obj *res);
bool coerce_string(struct workspace *wk, uint32_t node, obj val, obj *res);
bool coerce_string_array(struct workspace *wk, uint32_t node, obj arr, obj *res);
bool coerce_num_to_string(struct workspace *wk, uint32_t node, obj val, obj *res);
bool coerce_executable(struct workspace *wk, uint32_t node, obj val, obj *res, obj *args);
bool coerce_requirement(struct workspace *wk, struct args_kw *kw_required, enum requirement_type *requirement);
bool coerce_files(struct workspace *wk, uint32_t node, obj val, obj *res);
bool coerce_file(struct workspace *wk, uint32_t node, obj val, obj *res);
bool coerce_dirs(struct workspace *wk, uint32_t node, obj val, obj *res);
bool coerce_output_files(struct workspace *wk, uint32_t node, obj val, const char *output_dir, obj *res);
bool coerce_include_dirs(struct workspace *wk, uint32_t node, obj val, bool is_system, obj *res);
enum machine_kind coerce_machine_kind(struct workspace *wk, struct args_kw *native_kw);
bool coerce_truthiness(struct workspace *wk, obj o);
#endif

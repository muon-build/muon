/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_STRING_H
#define MUON_FUNCTIONS_STRING_H
#include "lang/func_lookup.h"

enum format_cb_result {
	format_cb_found,
	format_cb_not_found,
	format_cb_error,
	format_cb_skip,
};

bool version_compare(const struct str *ver1, const struct str *_ver2);
bool version_compare_list(struct workspace *wk, const struct str *ver, obj cmp_arr);

typedef enum format_cb_result(
	(*string_format_cb)(struct workspace *wk, uint32_t node, void *ctx, const struct str *key, uint32_t *elem));

bool string_format(struct workspace *wk, uint32_t err_node, obj str, obj *res, void *ctx, string_format_cb cb);

extern const struct func_impl impl_tbl_string[];
extern const struct func_impl impl_tbl_string_internal[];
#endif

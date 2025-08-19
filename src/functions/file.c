/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "functions/compiler.h"
#include "functions/file.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"

static bool
file_ends_with_suffix(struct workspace *wk, obj file, const char *suffixes[], uint32_t len)
{
	const struct str *s = get_str(wk, *get_obj_file(wk, file));

	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (str_endswith(s, &STRL(suffixes[i]))) {
			return true;
		}
	}

	return false;
}

bool
file_is_dynamic_lib(struct workspace *wk, obj file)
{
	const char *exts[] = { COMPILER_DYNAMIC_LIB_EXTS };
	return file_ends_with_suffix(wk, file, exts, ARRAY_LEN(exts));
}

bool
file_is_static_lib(struct workspace *wk, obj file)
{
	const char *exts[] = { COMPILER_STATIC_LIB_EXTS };
	return file_ends_with_suffix(wk, file, exts, ARRAY_LEN(exts));
}

bool
file_is_linkable(struct workspace *wk, obj file)
{
	return  file_is_static_lib(wk, file) || file_is_dynamic_lib(wk, file);
}

FUNC_IMPL(file, full_path, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = *get_obj_file(wk, self);
	return true;
}

FUNC_REGISTER(file)
{
	FUNC_IMPL_REGISTER(file, full_path);
}

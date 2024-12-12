/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "lang/func_lookup.h"
#include "functions/file.h"
#include "lang/typecheck.h"
#include "log.h"

static bool
file_ends_with_suffix(struct workspace *wk, obj file, const char *suffixes[])
{
	const struct str *s = get_str(wk, *get_obj_file(wk, file));

	uint32_t i;
	for (i = 0; suffixes[i]; ++i) {
		if (str_endswith(s, &WKSTR(suffixes[i]))) {
			return true;
		}
	}

	return false;
}

bool
file_is_dynamic_lib(struct workspace *wk, obj file)
{
	return file_ends_with_suffix(wk, file, (const char *[]){ ".dll", ".lib", ".so", ".dylib", 0 });
}

bool
file_is_static_lib(struct workspace *wk, obj file)
{
	return file_ends_with_suffix(wk, file, (const char *[]){ ".a", 0 });
}

bool
file_is_linkable(struct workspace *wk, obj file)
{
	return  file_is_static_lib(wk, file) || file_is_dynamic_lib(wk, file);
}

static bool
func_file_full_path(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = *get_obj_file(wk, self);
	return true;
}

const struct func_impl impl_tbl_file[] = {
	{ "full_path", func_file_full_path, tc_string },
	{ NULL, NULL },
};

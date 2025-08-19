/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "external/libcurl.h"
#include "functions/modules/curl.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/mem.h"

FUNC_IMPL(module_curl, fetch, tc_string, func_impl_flag_impure, .desc = "Begin fetching a url using libcurl.  Only available if libcurl support is enabeld." )
{
	struct args_norm an[] = {
		{ tc_string, .desc = "the url to fetch" },
		ARG_TYPE_NULL,
	};
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	mc_init();

	uint8_t *buf;
	uint64_t len;
	enum mc_fetch_flag flags = 0;
	struct mc_fetch_stats stats;
	int32_t handle = mc_fetch_begin(get_cstr(wk, an[0].val), &buf, &len, flags);

	if (handle == -1) {
		return false;
	}

	bool ok = true;
	while (true) {
		switch (mc_fetch_collect(handle, &stats)) {
		case mc_fetch_collect_result_pending: break;
		case mc_fetch_collect_result_done: {
			*res = make_strn(wk, (char *)buf, len);
			goto done;
		}
		case mc_fetch_collect_result_error: {
			ok = false;
			goto done;
		}
		}

		mc_wait(1000);
	}

done:
	z_free(buf);
	mc_deinit();
	return ok;
}

FUNC_REGISTER(module_curl)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_curl, fetch);
	}
}

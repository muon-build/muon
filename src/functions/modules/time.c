/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>

#include "functions/modules/time.h"
#include "lang/typecheck.h"
#include "platform/timer.h"

FUNC_IMPL(module_time, timer_start, tc_number, .flags = func_impl_flag_impure)
{
	if (!pop_args(wk, 0, 0)) {
		return false;
	}

	struct timer *t = ar_make(wk->a, struct timer);
	timer_start(t);
	*res = make_number(wk, (uintptr_t)t);
	return true;
}

FUNC_IMPL(module_time, timer_read, tc_number, .flags = func_impl_flag_impure)
{
	struct args_norm an[] = { { tc_number }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, 0)) {
		return false;
	}

	struct timer *t = (struct timer *)(uintptr_t)get_obj_number(wk, an[0].val);
	float secs = timer_read(t);

	*res = make_number(wk, secs * 1000000000.0f);
	return true;
}

FUNC_IMPL(module_time, nanosleep, 0, .flags = func_impl_flag_impure)
{
	struct args_norm an[] = { { tc_number }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, 0)) {
		return false;
	}

	timer_sleep(get_obj_number(wk, an[0].val));
	return true;
}

FUNC_REGISTER(module_time)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_time, timer_start);
		FUNC_IMPL_REGISTER(module_time, timer_read);
		FUNC_IMPL_REGISTER(module_time, nanosleep);
	}
}

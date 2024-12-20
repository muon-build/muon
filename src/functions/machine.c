/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "error.h"
#include "functions/machine.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"
#include "log.h"
#include "machines.h"
#include "platform/assert.h"

static struct machine_definition *
get_machine_for_self(struct workspace *wk, obj self)
{
	switch (get_obj_machine(wk, self)) {
	case machine_kind_build: return &build_machine;
	case machine_kind_host: return &host_machine;
	}

	UNREACHABLE_RETURN;
}

static bool
func_machine_system(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = make_str(wk, machine_system_to_s(m->sys));
	return true;
}

static bool
func_machine_endian(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);
	enum endianness e = m->endianness;

	const char *s = NULL;
	switch (e) {
	case little_endian: s = "little"; break;
	case big_endian: s = "big"; break;
	}

	*res = make_str(wk, s);
	return true;
}

static bool
func_machine_cpu_family(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = make_str(wk, m->cpu_family);
	return true;
}

static bool
func_machine_cpu(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = make_str(wk, m->cpu);
	return true;
}

static bool
func_machine_kernel(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = make_str(wk, machine_system_to_kernel_name(m->sys));
	return true;
}

static bool
func_machine_subsystem(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	LOG_W("machine.subsystem is not supported");
	*res = make_str(wk, "");
	return true;
}

const struct func_impl impl_tbl_machine[] = {
	{ "cpu", func_machine_cpu, tc_string },
	{ "cpu_family", func_machine_cpu_family, tc_string },
	{ "endian", func_machine_endian, tc_string },
	{ "system", func_machine_system, tc_string },
	{ "kernel", func_machine_kernel, tc_string },
	{ "subsystem", func_machine_subsystem, tc_string },
	{ NULL, NULL },
};

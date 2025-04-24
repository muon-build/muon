/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stddef.h>
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
	default: break;
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
	case endianness_uninitialized: s = "?"; break;
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

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = make_str(wk, machine_subsystem_to_s(m->subsystem));
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

struct machine_props {
	enum machine_system system;
	enum machine_subsystem subsystem;
	enum endianness endian;
	const char *cpu;
	const char *cpu_family;
};

static bool
func_machine_set_props(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = { { obj_dict }, ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	if (vm_enum(wk, endianness)) {
		vm_enum_value(wk, endianness, big_endian);
		vm_enum_value(wk, endianness, little_endian);
	};

	if (vm_enum(wk, machine_system)) {
		vm_enum_value_prefixed(wk, machine_system, unknown);
		vm_enum_value_prefixed(wk, machine_system, dragonfly);
		vm_enum_value_prefixed(wk, machine_system, freebsd);
		vm_enum_value_prefixed(wk, machine_system, gnu);
		vm_enum_value_prefixed(wk, machine_system, haiku);
		vm_enum_value_prefixed(wk, machine_system, linux);
		vm_enum_value_prefixed(wk, machine_system, netbsd);
		vm_enum_value_prefixed(wk, machine_system, openbsd);
		vm_enum_value_prefixed(wk, machine_system, sunos);
		vm_enum_value_prefixed(wk, machine_system, android);
		vm_enum_value_prefixed(wk, machine_system, emscripten);
		vm_enum_value_prefixed(wk, machine_system, windows);
		vm_enum_value_prefixed(wk, machine_system, cygwin);
		vm_enum_value_prefixed(wk, machine_system, msys2);
		vm_enum_value_prefixed(wk, machine_system, darwin);
	}

	if (vm_enum(wk, machine_subsystem)) {
		vm_enum_value_prefixed(wk, machine_subsystem, macos);
		vm_enum_value_prefixed(wk, machine_subsystem, ios);
	}

	if (vm_struct(wk, machine_props)) {
		vm_struct_member(wk, machine_props, cpu, vm_struct_type_str);
		vm_struct_member(wk, machine_props, cpu_family, vm_struct_type_str);
		vm_struct_member(wk, machine_props, endian, vm_struct_type_enum(wk, endianness));
		vm_struct_member(wk, machine_props, system, vm_struct_type_enum(wk, machine_system));
	}

	struct machine_props props = { 0 };

	if (!vm_obj_to_struct(wk, machine_props, an[0].val, &props)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	if (props.cpu) {
		cstr_copy(m->cpu, props.cpu, sizeof(m->cpu));
	}

	if (props.cpu_family) {
		cstr_copy(m->cpu_family, props.cpu_family, sizeof(m->cpu_family));
	}

	if (props.system) {
		m->sys = props.system;
	}

	if (props.subsystem) {
		m->subsystem = props.subsystem;
	}

	if (props.endian) {
		m->endianness = props.endian;
	}

	return true;
}

const struct func_impl impl_tbl_machine_internal[] = {
	{ "cpu", func_machine_cpu, tc_string },
	{ "cpu_family", func_machine_cpu_family, tc_string },
	{ "endian", func_machine_endian, tc_string },
	{ "system", func_machine_system, tc_string },
	{ "kernel", func_machine_kernel, tc_string },
	{ "subsystem", func_machine_subsystem, tc_string },
	{ "set_props", func_machine_set_props, },
	{ NULL, NULL },
};

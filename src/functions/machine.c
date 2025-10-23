/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stddef.h>
#include <string.h>

#include "buf_size.h"
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

FUNC_IMPL(machine, system, COMPLEX_TYPE_PRESET(tc_cx_enum_machine_system), func_impl_flag_impure )
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = str_enum_get(wk, complex_type_enum_get(wk, tc_cx_enum_machine_system), machine_system_to_s(m->sys));
	return true;
}

FUNC_IMPL(machine, subsystem, COMPLEX_TYPE_PRESET(tc_cx_enum_machine_subsystem), func_impl_flag_impure )
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	if (!m->subsystem) {
		vm_error(wk, "subsystem is undefined");
		return false;
	}

	*res = str_enum_get(
		wk, complex_type_enum_get(wk, tc_cx_enum_machine_subsystem), machine_subsystem_to_s(m->subsystem));
	return true;
}

FUNC_IMPL(machine, endian, COMPLEX_TYPE_PRESET(tc_cx_enum_machine_endian), func_impl_flag_impure )
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	const char *s = NULL;
	switch (m->endianness) {
	case endianness_uninitialized: UNREACHABLE; break;
	case little_endian: s = "little"; break;
	case big_endian: s = "big"; break;
	}

	*res = str_enum_get(wk, complex_type_enum_get(wk, tc_cx_enum_machine_endian), s);
	return true;
}

FUNC_IMPL(machine, cpu_family, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = make_str(wk, m->cpu_family);
	return true;
}

FUNC_IMPL(machine, cpu, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = make_str(wk, m->cpu);
	return true;
}

FUNC_IMPL(machine, kernel, tc_string, func_impl_flag_impure)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	*res = make_str(wk, machine_system_to_kernel_name(m->sys));
	return true;
}

struct machine_props {
	enum machine_system system;
	enum machine_subsystem subsystem;
	enum endianness endian;
	const struct str *cpu;
	const struct str *cpu_family;
};

FUNC_IMPL(machine, set_props, 0, func_impl_flag_impure)
{
	if (vm_enum(wk, endianness)) {
		vm_enum_value_(wk, "endianness", "big", big_endian);
		vm_enum_value_(wk, "endianness", "little", little_endian);
	};

	if (vm_enum(wk, machine_system)) {
#define MACHINE_ENUM(id) vm_enum_value_prefixed(wk, machine_system, id);
		FOREACH_MACHINE_SYSTEM(MACHINE_ENUM)
#undef MACHINE_ENUM
	}

	if (vm_enum(wk, machine_subsystem)) {
#define MACHINE_ENUM(id) vm_enum_value_prefixed(wk, machine_subsystem, id);
		FOREACH_MACHINE_SUBSYSTEM(MACHINE_ENUM)
#undef MACHINE_ENUM
	}

	if (vm_struct(wk, machine_props)) {
		vm_struct_member(wk, machine_props, cpu, vm_struct_type_str);
		vm_struct_member(wk, machine_props, cpu_family, vm_struct_type_str);
		vm_struct_member(wk, machine_props, endian, vm_struct_type_enum(wk, endianness));
		vm_struct_member(wk, machine_props, system, vm_struct_type_enum(wk, machine_system));
		vm_struct_member(wk, machine_props, subsystem, vm_struct_type_enum(wk, machine_subsystem));
	}

	struct args_norm an[] = { { obj_dict, .desc = vm_struct_docs(wk, machine_props, "accepted properties:\n```\n%s\n```") },
		ARG_TYPE_NULL };

	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	struct machine_props props = { 0 };

	if (!vm_obj_to_struct(wk, machine_props, an[0].val, &props)) {
		return false;
	}

	struct machine_definition *m = get_machine_for_self(wk, self);

	if (props.cpu) {
		cstr_copy(m->cpu, props.cpu);
	}

	if (props.cpu_family) {
		cstr_copy(m->cpu_family, props.cpu_family);
	}

	if (props.system) {
		m->sys = props.system;
	}

	m->subsystem = props.subsystem;

	if (props.endian) {
		m->endianness = props.endian;
	}

	return true;
}

FUNC_REGISTER(machine)
{
	FUNC_IMPL_REGISTER(machine, cpu);
	FUNC_IMPL_REGISTER(machine, cpu_family);
	FUNC_IMPL_REGISTER(machine, endian);
	FUNC_IMPL_REGISTER(machine, system);
	FUNC_IMPL_REGISTER(machine, kernel);
	FUNC_IMPL_REGISTER(machine, subsystem);

	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(machine, set_props);
	}
}

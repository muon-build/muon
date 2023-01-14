/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_FUNCTIONS_MACHINE_H
#define MUON_FUNCTIONS_MACHINE_H
#include "functions/common.h"

enum machine_system {
	machine_system_dragonfly,
	machine_system_freebsd,
	machine_system_gnu,
	machine_system_haiku,
	machine_system_linux,
	machine_system_netbsd,
	machine_system_openbsd,
	machine_system_sunos,
	machine_system_android,
	machine_system_emscripten,
	machine_system_windows,
	machine_system_cgywin,
	machine_system_msys2,
	machine_system_darwin,
	machine_system_unknown,
};

enum machine_system machine_system(void);
uint32_t machine_cpu_address_bits(void);

extern const struct func_impl_name impl_tbl_machine[];
#endif

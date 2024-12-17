#ifndef MUON_MACHINE_H
/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define MUON_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/uname.h"

enum machine_system {
	machine_system_uninitialized = 0,
	machine_system_unknown = 1,
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
	machine_system_cygwin,
	machine_system_msys2,
	machine_system_darwin,
};

enum machine_kind {
	machine_kind_build,
	machine_kind_host,
};
#define machine_kind_count 2 // Keep in sync with above

struct machine_definition {
	enum machine_kind kind;
	enum machine_system sys;
	enum endianness endianness;
	uint32_t address_bits;
	char cpu[128];
	char cpu_family[128];
	bool is_windows;
};

extern struct machine_definition build_machine, host_machine;

const char *machine_kind_to_s(enum machine_kind kind);
const char *machine_system_to_s(enum machine_system sys);
const char *machine_system_to_kernel_name(enum machine_system sys);

void machine_parse_and_apply_triplet(struct machine_definition *m, const char *s);

void machine_init(void);
#endif

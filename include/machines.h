#ifndef MUON_MACHINE_H
/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define MUON_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/uname.h"

/*
 * Work around non-standard gcc behavior.
 *
 * When gcc is invoked without a -std flag, it defines several nonstandard
 * macros, linux being one of them, which messes up our macro expansion.
 * The proper fix for this is to always pass -std=c99, but if we fix it here
 * then we can preserve the ability to compile the amalgam using gcc without
 * any additional flags.
 *
 * Reference:
 * - https://gcc.gnu.org/onlinedocs/gcc-5.3.0/cpp/System-specific-Predefined-Macros.html
 * - https://stackoverflow.com/a/19214007
 */
#ifdef linux
#undef linux
#endif

#define FOREACH_MACHINE_SYSTEM(_) \
	_(unknown) \
	_(dragonfly) \
	_(freebsd) \
	_(gnu) \
	_(haiku) \
	_(linux) \
	_(netbsd) \
	_(openbsd) \
	_(sunos) \
	_(android) \
	_(emscripten) \
	_(windows) \
	_(cygwin) \
	_(msys2) \
	_(darwin) \

enum machine_system {
	machine_system_uninitialized = 0,
#define MACHINE_ENUM(id) machine_system_##id,
FOREACH_MACHINE_SYSTEM(MACHINE_ENUM)
#undef MACHINE_ENUM
};

#define FOREACH_MACHINE_SUBSYSTEM(_) \
	_(unknown) \
	_(macos) \
	_(ios) \
	_(tvos) \
	_(visionos) \

enum machine_subsystem {
	machine_subsystem_uninitialized = 0,
#define MACHINE_ENUM(id) machine_subsystem_##id,
FOREACH_MACHINE_SUBSYSTEM(MACHINE_ENUM)
#undef MACHINE_ENUM
};

enum machine_kind {
	machine_kind_build,
	machine_kind_host,
	machine_kind_either,
};
#define machine_kind_count 2 // Keep in sync with above

struct machine_definition {
	enum machine_kind kind;
	enum machine_system sys;
	enum machine_subsystem subsystem;
	enum endianness endianness;
	uint32_t address_bits;
	char cpu[128];
	char cpu_family[128];
	bool is_windows;
};

extern struct machine_definition build_machine, host_machine;
extern const struct machine_definition *machine_definitions[machine_kind_count];

const char *machine_kind_to_s(enum machine_kind kind);
const char *machine_system_to_s(enum machine_system sys);
const char *machine_subsystem_to_s(enum machine_subsystem sys);
const char *machine_system_to_kernel_name(enum machine_system sys);

void machine_parse_and_apply_triplet(struct machine_definition *m, const char *s);

void machine_init(void);

bool machine_matches(enum machine_kind a, enum machine_kind b);
bool machine_definitions_eql(struct machine_definition *a, struct machine_definition *b);
#endif

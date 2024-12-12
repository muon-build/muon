/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "error.h"
#include "lang/string.h"
#include "machines.h"
#include "platform/uname.h"

struct machine_definition build_machine, host_machine;

static const char *known_cpu_families[] = {
	"aarch64",
	"alpha",
	"arc",
	"arm",
	"avr",
	"c2000",
	"csky",
	"dspic",
	"e2k",
	"ft32",
	"ia64",
	"loongarch64",
	"m68k",
	"microblaze",
	"mips",
	"mips64",
	"msp430",
	"parisc",
	"pic24",
	"ppc",
	"ppc64",
	"riscv32",
	"riscv64",
	"rl78",
	"rx",
	"s390",
	"s390x",
	"sh4",
	"sparc",
	"sparc64",
	"wasm32",
	"wasm64",
	"x86",
	"x86_64",
};

const char *
machine_kind_to_s(enum machine_kind kind)
{
	switch (kind) {
	case machine_kind_build: return "build";
	case machine_kind_host: return "host";
	}

	UNREACHABLE_RETURN;
}

const char *
machine_system_to_s(enum machine_system sys)
{
	switch (sys) {
	case machine_system_dragonfly: return "dragonfly";
	case machine_system_freebsd: return "freebsd";
	case machine_system_gnu: return "gnu";
	case machine_system_haiku: return "haiku";
	case machine_system_linux: return "linux";
	case machine_system_netbsd: return "netbsd";
	case machine_system_openbsd: return "openbsd";
	case machine_system_sunos: return "sunos";
	case machine_system_android: return "android";
	case machine_system_emscripten: return "emscripten";
	case machine_system_windows: return "windows";
	case machine_system_cygwin: return "cygwin";
	case machine_system_msys2: return "msys2";
	case machine_system_darwin: return "darwin";
	case machine_system_unknown: return "unknown";
	}

	UNREACHABLE_RETURN;
}

const char *
machine_system_to_kernel_name(enum machine_system sys)
{
	switch (sys) {
	case machine_system_freebsd: return "freebsd";
	case machine_system_openbsd: return "openbsd";
	case machine_system_netbsd: return "netbsd";
	case machine_system_windows: return "nt";
	case machine_system_android: return "linux";
	case machine_system_linux: return "linux";
	case machine_system_cygwin: return "nt";
	case machine_system_darwin: return "xnu";
	case machine_system_sunos: return "sunos";
	case machine_system_dragonfly: return "dragonfly";
	case machine_system_haiku: return "haiku";
	default: return "none";
	}
}

static enum machine_system
machine_system(const struct str *sysname)
{
	if (str_eql(sysname, &WKSTR("unknown"))) {
		return machine_system_unknown;
	}

	// The Cygwin environment for Windows
	if (str_startswith(sysname, &WKSTR("cygwin_nt"))) {
		return machine_system_cygwin;
	}

	// The MSYS2 environment for Windows
	if (str_startswith(sysname, &WKSTR("msys_nt"))) {
		return machine_system_msys2;
	}

	const struct {
		const char *name;
		enum machine_system sys;
	} map[] = {
		{ "darwin", machine_system_darwin }, // Either OSX or iOS
		{ "dragonfly", machine_system_dragonfly }, // DragonFly BSD
		{ "freebsd", machine_system_freebsd }, // FreeBSD and its derivatives
		{ "gnu", machine_system_gnu }, // GNU Hurd
		{ "haiku", machine_system_haiku },
		{ "linux", machine_system_linux },
		{ "netbsd", machine_system_netbsd },
		{ "openbsd", machine_system_openbsd },
		{ "sunos", machine_system_sunos }, // illumos and Solaris

		// TODO: These probably need more than just a simple mapping
		{ "android", machine_system_android }, // By convention only, subject to change
		{ "emscripten", machine_system_emscripten }, // Emscripten's Javascript environment
		{ "windows", machine_system_windows }, // Any version of Windows
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(map); ++i) {
		if (str_eql(&WKSTR(map[i].name), sysname)) {
			return map[i].sys;
		}
	}

	return machine_system_unknown;
}

static void
machine_cpu(struct machine_definition *m, const struct str *mstr)
{
	const char *norm = 0;

	if (str_startswith(mstr, &WKSTR("aarch64"))) {
		norm = "aarch64";
	} else if (str_startswith(mstr, &WKSTR("earm"))) {
		norm = "arm";
	} else if (str_startswith(mstr, &WKSTR("mips"))) {
		if (strstr(mstr->s, "64")) {
			norm = "mips64";
		} else {
			norm = "mips";
		}
	} else {
		const char *map[][2] = { { "amd64", "x86_64" }, { "x64", "x86_64" }, { "i86pc", "x86_64" }, 0 };

		uint32_t i;
		for (i = 0; map[i][0]; ++i) {
			if (str_eql(&WKSTR(map[i][0]), mstr)) {
				norm = map[i][1];
				break;
			}
		}
	}

	if (!norm) {
		norm = mstr->s;
	}

	uint32_t len = strlen(norm) + 1;
	assert(sizeof(m->cpu) >= len);
	memcpy(m->cpu, norm, len);
}

static void
machine_cpu_family(struct machine_definition *m)
{
	const char *norm = 0;

	if (m->cpu[0] == 'i' && str_endswith(&WKSTR(m->cpu), &WKSTR("86"))) {
		norm = "x86";
	} else if (str_startswith(&WKSTR(m->cpu), &WKSTR("arm64"))) {
		norm = "aarch64";
	} else if (str_startswith(&WKSTR(m->cpu), &WKSTR("arm"))) {
		norm = "arm";
	} else if (str_startswith(&WKSTR(m->cpu), &WKSTR("powerpc64"))
		   || str_startswith(&WKSTR(m->cpu), &WKSTR("ppc64"))) {
		norm = "ppc64";
	} else if (str_startswith(&WKSTR(m->cpu), &WKSTR("powerpc")) || str_startswith(&WKSTR(m->cpu), &WKSTR("ppc"))) {
		norm = "ppc";
	} else {
		const char *map[][2] = {
			{ "bepc", "x86" },
			{ "macppc", "ppc" },
			{ "power macintosh", "ppc" },
			{ "amd64", "x86_64" },
			{ "x64", "x86_64" },
			{ "i86pc", "x86_64" },
			{ "sun4u", "sparc64" },
			{ "sun4v", "sparc64" },
			{ "ip30", "mpis64" },
			{ "ip35", "mpis64" },
			0,
		};

		uint32_t i;
		for (i = 0; map[i][0]; ++i) {
			if (str_eql(&WKSTR(map[i][0]), &WKSTR(m->cpu))) {
				norm = map[i][1];
				break;
			}
		}
	}

	if (!norm) {
		norm = m->cpu;
	}

	uint32_t len = strlen(norm) + 1;
	assert(sizeof(m->cpu_family) >= len);
	memcpy(m->cpu_family, norm, len);

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(known_cpu_families); ++i) {
		if (strcmp(norm, known_cpu_families[i]) == 0) {
			break;
		}
	}

	if (i == ARRAY_LEN(known_cpu_families)) {
		LOG_W("%s machine has unknown cpu family '%s'", machine_kind_to_s(m->kind), m->cpu_family);
	}
}

static uint32_t
machine_cpu_address_bits(struct machine_definition *m)
{
	const char *is_64_bit[] = {
		"aarch64",
		"alpha",
		"ia64",
		"loongarch64",
		"mips64",
		"ppc64",
		"riscv64",
		"s390x",
		"sparc64",
		"wasm64",
		"x86_64",
	};

	const char *fam = m->cpu_family;

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(is_64_bit); ++i) {
		if (strcmp(fam, is_64_bit[i]) == 0) {
			return 64;
		}
	}

	return 32;
}

/* arm-none-eabi */
/* armv7a-none-eabi */
/* arm-linux-gnueabihf */
/* arm-none-linux-gnueabi */
/* i386-pc-linux-gnu */
/* x86_64-apple-darwin10 */
/* i686-w64-windows-gnu # same as i686-w64-mingw32 */
/* x86_64-pc-linux-gnu # from ubuntu 64 bit */
/* x86_64-unknown-windows-cygnus # cygwin 64-bit */
/* x86_64-w64-windows-gnu # same as x86_64-w64-mingw32 */
/* i686-pc-windows-gnu # MSVC */
/* x86_64-pc-windows-gnu # MSVC 64-BIT */

void
machine_parse_and_apply_triplet(struct machine_definition *m, const char *s)
{
	struct str parts[4] = { 0 };
	uint32_t i;
	const char *end;

	i = 0;
	while ((end = strchr(s, '-')) && i < ARRAY_LEN(parts) - 1) {
		parts[i].s = s;
		parts[i].len = end - s;
		s = end + 1;
		++i;
	}

	// Add the trailing part, trimming whitespace
	parts[i].s = s;
	while (!strchr(" \n\r\t", parts[i].s[parts[i].len])) {
		++parts[i].len;
	}
	if (parts[i].len) {
		++i;
	}

	// Fill in missing slots
	if (i == 0) {
		parts[0] = WKSTR("unknown");
		parts[1] = WKSTR("unknown");
		parts[2] = WKSTR("unknown");
		parts[3] = WKSTR("unknown");
	} else if (i == 1) {
		parts[1] = WKSTR("unknown");
		parts[2] = WKSTR("unknown");
		parts[3] = WKSTR("unknown");
	} else if (i == 2) {
		parts[2] = parts[1];
		parts[1] = WKSTR("unknown");
		parts[3] = WKSTR("unknown");
	} else if (i == 3) {
		parts[3] = parts[2];
		parts[2] = parts[1];
		parts[1] = WKSTR("unknown");
	} else if (i == 4) {
		// nothing to do, we got all the parts
	} else {
		UNREACHABLE;
	}

	L("reconstructed triplet: %.*s-%.*s-%.*s-%.*s",
		parts[0].len,
		parts[0].s,
		parts[1].len,
		parts[1].s,
		parts[2].len,
		parts[2].s,
		parts[3].len,
		parts[3].s);

	// Not doing anything with it so far.
}

void
machine_init(void)
{
	static bool init = false;
	if (init) {
		return;
	}
	init = true;

	const char *mstr = uname_machine();
	const char *sysstr = uname_sysname();

	build_machine.kind = machine_kind_build;
	build_machine.sys = machine_system(&WKSTR(sysstr));
	machine_cpu(&build_machine, &WKSTR(mstr));
	machine_cpu_family(&build_machine);
	build_machine.endianness = uname_endian();
	build_machine.address_bits = machine_cpu_address_bits(&build_machine);
	build_machine.is_windows = build_machine.sys == machine_system_windows
				   || build_machine.sys == machine_system_cygwin;

	host_machine = build_machine;
	host_machine.kind = machine_kind_host;
}

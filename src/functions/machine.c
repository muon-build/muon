/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/machine.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/uname.h"

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

static bool
func_machine_system(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *sysname;
	if (!uname_sysname(&sysname)) {
		return false;
	}

	const char *map[][2] = {
		{ "darwin", "darwin" }, // Either OSX or iOS
		{ "dragonfly", "dragonfly" }, // DragonFly BSD
		{ "freebsd", "freebsd" }, // FreeBSD and its derivatives
		{ "gnu", "gnu" }, // GNU Hurd
		{ "haiku", "haiku" },
		{ "linux", "linux" },
		{ "netbsd", "netbsd" },
		{ "openbsd", "openbsd" },
		{ "sunos", "sunos" }, // illumos and Solaris

		// TODO: These probably need more than just a simple mapping
		{ "android", "android" }, // By convention only, subject to change
		{ "cygwin", "cygwin" }, // The Cygwin environment for Windows
		{ "emscripten", "emscripten" }, // Emscripten's Javascript environment
		{ "windows", "windows" }, // Any version of Windows
		0
	};

	uint32_t i;
	for (i = 0; map[i][0]; ++i) {
		if (strcmp(map[i][0], sysname) == 0) {
			*res = make_str(wk, map[i][1]);
			return true;
		}
	}

	LOG_E("unknown sysname '%s'", sysname);
	return false;
}

static bool
func_machine_endian(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	enum endianness e;
	if (!uname_endian(&e)) {
		return false;
	}

	const char *s = NULL;
	switch (e) {
	case little_endian:
		s = "little";
		break;
	case big_endian:
		s = "big";
		break;
	}

	*res = make_str(wk, s);
	return true;
}

static void
machine_cpu_normalize_base(const char **machine_cstr, const char **norm)
{
	const struct str *machine;

	*norm = NULL;

	if (!uname_machine(machine_cstr)) {
		LOG_E("unable to determine cpu information");
		*machine_cstr = "unknown";
		return;
	}

	machine = &WKSTR(*machine_cstr);

	if (str_startswith(machine, &WKSTR("aarch64"))) {
		*norm = "aarch64";
	} else if (str_startswith(machine, &WKSTR("earm"))) {
		*norm = "arm";
	} else if (str_startswith(machine, &WKSTR("mips"))) {
		if (strstr(machine->s, "64")) {
			*norm = "mips64";
		} else {
			*norm = "mips";
		}
	} else {
		const char *map[][2] = {
			{ "amd64", "x86_64" },
			{ "x64", "x86_64" },
			{ "i86pc", "x86_64" },
			0
		};

		uint32_t i;
		for (i = 0; map[i][0]; ++i) {
			if (str_eql(&WKSTR(map[i][0]), machine)) {
				*norm = map[i][1];
				break;
			}
		}
	}
}

static bool
func_machine_cpu_family(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	const char *machine_cstr, *norm;
	const struct str *machine;

	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	machine_cpu_normalize_base(&machine_cstr, &norm);
	machine = &WKSTR(machine_cstr);

	if (norm) {
		goto done;
	}

	if (machine->s[0] == 'i' && str_endswith(machine, &WKSTR("86"))) {
		norm = "x86";
	} else if (str_startswith(machine, &WKSTR("arm"))) {
		norm = "arm";
	} else if (str_startswith(machine, &WKSTR("powerpc64"))
		   || str_startswith(machine, &WKSTR("ppc64"))) {
		norm = "ppc64";
	} else if (str_startswith(machine, &WKSTR("powerpc"))
		   || str_startswith(machine, &WKSTR("ppc"))) {
		norm = "ppc";
	} else {
		const char *map[][2] = {
			{ "bepc", "x86" },
			{ "arm64", "aarch64" },
			{ "macppc", "ppc" },
			{ "power macintosh", "ppc" },
			{ "amd64", "x86_64" },
			{ "x64", "x86_64" },
			{ "i86pc", "x86_64" },
			{ "sun4u", "sparc64" },
			{ "sun4v", "sparc64" },
			{ "ip30", "mpis64" },
			{ "ip35", "mpis64" },
			0
		};

		uint32_t i;
		for (i = 0; map[i][0]; ++i) {
			if (str_eql(&WKSTR(map[i][0]), machine)) {
				norm = map[i][1];
				break;
			}
		}
	}

done:
	if (!norm) {
		norm = machine->s;
	}

	*res = make_str(wk, norm);

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(known_cpu_families); ++i) {
		if (strcmp(norm, known_cpu_families[i]) == 0) {
			return true;
		}
	}

	LOG_W("returning unknown cpu family '%s'", machine->s);
	return true;
}

static bool
func_machine_cpu(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	const char *machine_cstr, *norm;

	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	machine_cpu_normalize_base(&machine_cstr, &norm);
	if (!norm) {
		norm = machine_cstr;
	}

	*res = make_str(wk, norm);
	return true;
}

const struct func_impl_name impl_tbl_machine[] = {
	{ "cpu", func_machine_cpu, tc_string },
	{ "cpu_family", func_machine_cpu_family, tc_string },
	{ "endian", func_machine_endian, tc_string },
	{ "system", func_machine_system, tc_string },
	{ NULL, NULL },
};

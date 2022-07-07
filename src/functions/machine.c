#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/machine.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/uname.h"

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
		{ "Darwin", "darwin" }, // Either OSX or iOS
		{ "DragonFly", "dragonfly" }, // DragonFly BSD
		{ "FreeBSD", "freebsd" }, // FreeBSD and its derivatives
		{ "GNU", "gnu" }, // GNU Hurd
		{ "Haiku", "haiku" },
		{ "Linux", "linux" },
		{ "NetBSD", "netbsd" },
		{ "OpenBSD", "openbsd" },
		{ "SunOS", "sunos" }, // illumos and Solaris

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

static bool
func_machine_cpu_family(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *machine;
	if (!uname_machine(&machine)) {
		return false;
	}

	const char *map[][2] = {
		{ "aarch64", "aarch64" }, // 64 bit ARM processor
		{ "alpha", "alpha" }, // DEC Alpha processor
		{ "arc", "arc" }, // 32 bit ARC processor
		{ "arm", "arm" }, // 32 bit ARM processor
		{ "avr", "avr" }, // Atmel AVR processor
		{ "c2000", "c2000" }, // 32 bit C2000 processor
		{ "csky", "csky" }, // 32 bit CSky processor
		{ "dspic", "dspic" }, // 16 bit Microchip dsPIC
		{ "e2k", "e2k" }, // MCST Elbrus processor
		{ "ft32", "ft32" }, // 32 bit Bridgetek MCU
		{ "ia64", "ia64" }, // Itanium processor
		{ "loongarch64", "loongarch64" }, // 64 bit Loongson processor
		{ "m68k", "m68k" }, // Motorola 68000 processor
		{ "microblaze", "microblaze" }, // MicroBlaze processor
		{ "mips", "mips" }, // 32 bit MIPS processor
		{ "mips64", "mips64" }, // 64 bit MIPS processor
		{ "msp430", "msp430" }, // 16 bit MSP430 processor
		{ "parisc", "parisc" }, // HP PA-RISC processor
		{ "pic24", "pic24" }, // 16 bit Microchip PIC24
		{ "ppc", "ppc" }, // 32 bit PPC processors
		{ "ppc64", "ppc64" }, // 64 bit PPC processors
		{ "ppc64le", "ppc64" },
		{ "riscv32", "riscv32" }, // 32 bit RISC-V Open ISA
		{ "riscv64", "riscv64" }, // 64 bit RISC-V Open ISA
		{ "rl78", "rl78" }, // Renesas RL78
		{ "rx", "rx" }, // Renesas RX 32 bit MCU
		{ "s390", "s390" }, // IBM zSystem s390
		{ "s390x", "s390x" }, // IBM zSystem s390x
		{ "sh4", "sh4" }, // SuperH SH-4
		{ "sparc", "sparc" }, // 32 bit SPARC
		{ "sparc64", "sparc64" }, // SPARC v9 processor
		{ "wasm32", "wasm32" }, // 32 bit Webassembly
		{ "wasm64", "wasm64" }, // 64 bit Webassembly
		{ "i686", "x86" }, // 32 bit x86 processor
		{ "x86_64", "x86_64" }, // 64 bit x86 processor
		0
	};

	uint32_t i;
	for (i = 0; map[i][0]; ++i) {
		if (strcmp(map[i][0], machine) == 0) {
			*res = make_str(wk, map[i][1]);
			return true;
		}
	}

	LOG_E("unknown cpu '%s'", machine);
	return false;
}

static bool
func_machine_cpu(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	const char *machine;
	if (!uname_machine(&machine)) {
		return false;
	}

	*res = make_str(wk, machine);
	return true;
}

const struct func_impl_name impl_tbl_machine[] = {
	{ "cpu", func_machine_cpu, tc_string },
	{ "cpu_family", func_machine_cpu_family, tc_string },
	{ "endian", func_machine_endian, tc_string },
	{ "system", func_machine_system, tc_string },
	{ NULL, NULL },
};

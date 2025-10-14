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
#include "platform/assert.h"
#include "platform/uname.h"

struct machine_definition build_machine, host_machine;
const struct machine_definition *machine_definitions[machine_kind_count] = {
	[machine_kind_build] = &build_machine,
	[machine_kind_host] = &host_machine,
};

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
	case machine_kind_either: return "either";
	}

	UNREACHABLE_RETURN;
}

const char *
machine_system_to_s(enum machine_system sys)
{
	switch (sys) {
	case machine_system_uninitialized: return "<uninitialized>";
#define MACHINE_ENUM(id) \
	case machine_system_##id: return #id;
		FOREACH_MACHINE_SYSTEM(MACHINE_ENUM)
#undef MACHINE_ENUM
	}

	UNREACHABLE_RETURN;
}

const char *
machine_subsystem_to_s(enum machine_subsystem sys)
{
	switch (sys) {
	case machine_subsystem_uninitialized: return "<uninitialized>";
#define MACHINE_ENUM(id) \
	case machine_subsystem_##id: return #id;
		FOREACH_MACHINE_SUBSYSTEM(MACHINE_ENUM)
#undef MACHINE_ENUM
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
	if (str_eql(sysname, &STR("unknown"))) {
		return machine_system_unknown;
	}

	// The Cygwin environment for Windows
	if (str_startswith(sysname, &STR("cygwin_nt"))) {
		return machine_system_cygwin;
	}

	// The MSYS2 environment for Windows
	if (str_startswith(sysname, &STR("msys_nt"))) {
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
		if (str_eql(&STRL(map[i].name), sysname)) {
			return map[i].sys;
		}
	}

	return machine_system_unknown;
}

static void
machine_cpu(struct machine_definition *m, const struct str *mstr)
{
	const char *norm = 0;

	if (str_startswith(mstr, &STR("aarch64"))) {
		norm = "aarch64";
	} else if (str_startswith(mstr, &STR("earm"))) {
		norm = "arm";
	} else if (str_startswith(mstr, &STR("mips"))) {
		if (strstr(mstr->s, "64")) {
			norm = "mips64";
		} else {
			norm = "mips";
		}
	} else {
		const char *map[][2] = { { "amd64", "x86_64" }, { "x64", "x86_64" }, { "i86pc", "x86_64" }, 0 };

		uint32_t i;
		for (i = 0; map[i][0]; ++i) {
			if (str_eql(&STRL(map[i][0]), mstr)) {
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

static bool
machine_cpu_family(struct machine_definition *m)
{
	const char *norm = 0;

	if (m->cpu[0] == 'i' && str_endswith(&STRL(m->cpu), &STR("86"))) {
		norm = "x86";
	} else if (str_startswith(&STRL(m->cpu), &STR("arm64"))) {
		norm = "aarch64";
	} else if (str_startswith(&STRL(m->cpu), &STR("arm"))) {
		norm = "arm";
	} else if (str_startswith(&STRL(m->cpu), &STR("powerpc64")) || str_startswith(&STRL(m->cpu), &STR("ppc64"))) {
		norm = "ppc64";
	} else if (str_startswith(&STRL(m->cpu), &STR("powerpc")) || str_startswith(&STRL(m->cpu), &STR("ppc"))) {
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
			{ "ip30", "mips64" },
			{ "ip35", "mips64" },
			0,
		};

		uint32_t i;
		for (i = 0; map[i][0]; ++i) {
			if (str_eql(&STRL(map[i][0]), &STRL(m->cpu))) {
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
		return false;
	}
	return true;
}

static bool
machine_cpu_is_known(const struct str *cpu)
{
	if (!cpu->len) {
		return false;
	}
	struct machine_definition m = { 0 };
	machine_cpu(&m, cpu);
	return machine_cpu_family(&m);
}

static bool
machine_vendor_is_known(const struct str *s)
{
	if (!s->len) {
		return false;
	}
	const char *known_vendors[] = {
		"apple",
		"pc",
		"scei",
		"sie",
		"fsl",
		"ibm",
		"img",
		"mti",
		"nvidia",
		"csr",
		"amd",
		"mesa",
		"suse",
		"oe",
		"intel",
		"meta",
	};

	for (uint32_t i = 0; i < ARRAY_LEN(known_vendors); ++i) {
		if (str_eql(s, &STRL(known_vendors[i]))) {
			return true;
		}
	}

	return false;
}

static bool
machine_env_is_known(const struct str *s)
{
	if (!s->len) {
		return false;
	}
	const char *known_abis[] = {
		"eabihf",
		"eabi",
		"gnuabin32",
		"gnuabi64",
		"gnueabihft64",
		"gnueabihf",
		"gnueabit64",
		"gnueabi",
		"gnuf32",
		"gnuf64",
		"gnusf",
		"gnux32",
		"gnu_ilp32",
		"code16",
		"gnut64",
		"gnu",
		"android",
		"muslabin32",
		"muslabi64",
		"musleabihf",
		"musleabi",
		"muslf32",
		"muslsf",
		"muslx32",
		"muslwali",
		"musl",
		"msvc",
		"itanium",
		"cygnus",
		"coreclr",
		"simulator",
		"macabi",
		"pixel",
		"vertex",
		"geometry",
		"hull",
		"domain",
		"compute",
		"library",
		"raygeneration",
		"intersection",
		"anyhit",
		"closesthit",
		"miss",
		"callable",
		"mesh",
		"amplification",
		"rootsignature",
		"opencl",
		"ohos",
		"pauthtest",
		"llvm",
		"mlibc",
		"mtia",
	};

	for (uint32_t i = 0; i < ARRAY_LEN(known_abis); ++i) {
		if (str_startswith(s, &STRL(known_abis[i]))) {
			return true;
		}
	}

	return false;
}

static bool
machine_system_is_known(const struct str *s)
{
	if (!s->len) {
		return false;
	}
	return str_eql(s, &STR("none")) || machine_system(s) != machine_system_unknown;
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

static void
machine_str_swap(struct str *a, struct str *b)
{
	struct str tmp = *a;
	*a = *b;
	*b = tmp;
}

// ported from llvm:
// https://llvm.org/doxygen/Triple_8cpp_source.html
void
machine_parse_triple(const struct str *str, struct target_triple *t)
{
	L("parsing triple: '%.*s'", str->len, str->s);

	*t = (struct target_triple){ 0 };
	struct str *parts[] = { &t->arch, &t->vendor, &t->system, &t->env };
	uint32_t parts_len = 0;
	{
		for (const char *s = str->s; parts_len < ARRAY_LEN(parts) && s < str->s + str->len; ++parts_len) {
			const char *end;
			if (!(end = memchr(s, '-', str->len - (s - str->s)))) {
				end = str->s + str->len;
			}

			*parts[parts_len] = (struct str){
				.s = s,
				.len = end - s,
			};
			s = end + 1;
		}

		for (uint32_t i = 0; i < parts_len; ++i) {
			str_strip_in_place(parts[i], 0, 0);

			// replace unknown / none with empty string
			if (str_eql(parts[i], &STR("unknown"))) {
				*parts[i] = (struct str) { 0 };
			}
		}
	}

	// Note which components are already in their final position.  These will not
	// be moved.
	bool found[4] = {
		machine_cpu_is_known(parts[0]),
		machine_vendor_is_known(parts[1]),
		machine_system_is_known(parts[2]),
		machine_env_is_known(parts[3]),
	};

	// If they are not there already, permute the components into their canonical
	// positions by seeing if they parse as a valid architecture, and if so moving
	// the component to the architecture position etc.
	for (uint32_t pos = 0; pos < 4; ++pos) {
		if (found[pos]) {
			continue; // Already in the canonical position.
		}

		for (uint32_t i = 0; i < parts_len; ++i) {
			// Do not reparse any components that already matched.
			if (i < 4 && found[i]) {
				continue;
			}

			// Does this component parse as valid for the target position?
			bool valid = false;
			switch (pos) {
			default: UNREACHABLE;
			case 0: valid = machine_cpu_is_known(parts[i]); break;
			case 1: valid = machine_vendor_is_known(parts[i]); break;
			case 2: valid = machine_system_is_known(parts[i]); break;
			case 3: valid = machine_vendor_is_known(parts[i]); break;
			}

			if (!valid) {
				continue; // Nope, try the next component.
			}

			// Move the component to the target position, pushing any non-fixed
			// components that are in the way to the right.  This tends to give
			// good results in the common cases of a forgotten vendor component
			// or a wrongly positioned environment.
			if (pos < i) {
				// Insert left, pushing the existing components to the right.  For
				// example, a-b-i386 -> i386-a-b when moving i386 to the front.
				struct str cur = { 0 }; // The empty component.
				// Replace the component we are moving with an empty component.
				machine_str_swap(&cur, parts[i]);
				// Insert the component being moved at Pos, displacing any existing
				// components to the right.
				for (uint32_t j = pos; cur.len; ++j) {
					// Skip over any fixed components.
					while (j < 4 && found[j])
					{
						++j;
					}
					// Place the component at the new position, getting the component
					// that was at this position - it will be moved right.
					machine_str_swap(&cur, parts[j]);
				}
			} else if (pos > i) {
				// Push right by inserting empty components until the component at Idx
				// reaches the target position Pos.  For example, pc-a -> -pc-a when
				// moving pc to the second position.
				do {
					// Insert one empty component at Idx.
					struct str cur = { 0 }; // The empty component.
					uint32_t j;
					for (j = i; j < parts_len;) {
						// Place the component at the new position, getting the component
						// that was at this position - it will be moved right.
						machine_str_swap(&cur, parts[j]);
						// If it was placed on top of an empty component then we are done.
						if (!cur.len)
						{
							break;
						}
						// Advance to the next component, skipping any fixed components.
						while (++j < 4 && found[j])
						{
						}
					}
					// The last component was pushed off the end - append it.
					if (cur.len)
					{
						if (j < 4) {
							machine_str_swap(&cur, parts[j]);
						}
					}

					// Advance Idx to the component's new position.
					while (++i < 4 && found[i])
					{
					}
				} while (i < pos); // Add more until the final position is reached.
			}
			assert(pos < parts_len && "Component moved wrong!");
			found[pos] = true;
			break;
		}
	}

	// If "none" is in the middle component in a three-component triple, treat it
	// as the OS (Components[2]) instead of the vendor (Components[1]).
	if (found[0] && !found[1] && !found[2] && found[3] && str_eql(parts[1], &STR("none")) && !parts[2]->len)
	{
		machine_str_swap(parts[1], parts[2]);
	}

	for (uint32_t i = 0; i < 4; ++i) {
		if (str_startswithi(parts[i], &STR("mingw"))) {
			*parts[2] = STR("windows");
			*parts[3] = STR("gnu");
			break;
		}
	}

	L("reconstructed triple: %.*s-%.*s-%.*s-%.*s",
		parts[0]->len,
		parts[0]->s,
		parts[1]->len,
		parts[1]->s,
		parts[2]->len,
		parts[2]->s,
		parts[3]->len,
		parts[3]->s);
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
	build_machine.sys = machine_system(&STRL(sysstr));
	build_machine.subsystem = build_machine.sys == machine_system_darwin ?
					  machine_subsystem_macos :
					  (enum machine_subsystem)build_machine.sys;
	machine_cpu(&build_machine, &STRL(mstr));
	if (!machine_cpu_family(&build_machine)) {
		LOG_W("%s machine has unknown cpu family '%s'", machine_kind_to_s(build_machine.kind), build_machine.cpu_family);
	}
	build_machine.endianness = uname_endian();
	build_machine.address_bits = machine_cpu_address_bits(&build_machine);
	build_machine.is_windows = build_machine.sys == machine_system_windows
				   || build_machine.sys == machine_system_cygwin;

	host_machine = build_machine;
	host_machine.kind = machine_kind_host;
}

bool
machine_matches(enum machine_kind a, enum machine_kind b)
{
	return a == machine_kind_either || a == b;
}

bool
machine_definitions_eql(struct machine_definition *a, struct machine_definition *b)
{
	return a->sys == b->sys && a->subsystem == b->subsystem && a->endianness == b->endianness
	       && a->address_bits == b->address_bits && strcmp(a->cpu, b->cpu) == 0;
}

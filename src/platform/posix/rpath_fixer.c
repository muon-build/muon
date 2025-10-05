/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifdef __sun
#include <gelf.h> /* Elf32_Dyn and similar on Solaris */
#endif

#include <elf.h>
#include <string.h>

#include "buf_size.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/rpath_fixer.h"
#include "platform/uname.h"

#ifndef DT_MIPS_RLD_MAP_REL
#define DT_MIPS_RLD_MAP_REL 1879048245
#endif

enum elf_class { elf_class_32, elf_class_64 };

struct elf {
	enum elf_class class;
	enum endianness endian;
	uint64_t shoff;
	uint16_t shentsize, shnum;
};

struct elf_section {
	uint64_t off, size;
	uint32_t type;
	uint32_t entsize;
	uint32_t len;
	bool found;
};

struct elf_dynstr {
	uint64_t off;
	uint32_t tag;
	uint32_t index;
	bool found;
};

union elf_hdrbuf {
	Elf64_Ehdr e64;
	Elf32_Ehdr e32;
	Elf64_Shdr s64;
	Elf32_Shdr s32;
	Elf64_Dyn d64;
	Elf32_Dyn d32;
	char bytes[BUF_SIZE_2k];
};

#define EHDR(BUF, CL, FLD) CL == elf_class_32 ? BUF.e32.FLD : BUF.e64.FLD
#define SHDR(BUF, CL, FLD) CL == elf_class_32 ? BUF.s32.FLD : BUF.s64.FLD
#define DHDR(BUF, CL, FLD) CL == elf_class_32 ? BUF.d32.FLD : BUF.d64.FLD

static bool
parse_elf(FILE *f, struct elf *elf)
{
	uint8_t ident[EI_NIDENT];
	size_t r = fread(ident, 1, EI_NIDENT, f);
	if (r != EI_NIDENT) {
		return false;
	}

	const uint8_t magic[4] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3 };

	if (memcmp(ident, magic, 4) != 0) {
		return false;
	}

	uint32_t hdr_size;
	switch (ident[EI_CLASS]) {
	case ELFCLASS32:
		hdr_size = sizeof(Elf32_Ehdr);
		elf->class = elf_class_32;
		break;
	case ELFCLASS64:
		hdr_size = sizeof(Elf64_Ehdr);
		elf->class = elf_class_64;
		break;
	case ELFCLASSNONE:
	default: return false;
	}

	switch (ident[EI_DATA]) {
	case ELFDATA2LSB: elf->endian = little_endian; break;
	case ELFDATA2MSB: elf->endian = big_endian; break;
	case ELFDATANONE:
	default: return false;
	}

	union elf_hdrbuf buf = { 0 };
	assert(hdr_size <= sizeof(buf));
	r = fread(&buf.bytes[EI_NIDENT], 1, hdr_size - EI_NIDENT, f);
	if (r != hdr_size - EI_NIDENT) {
		return false;
	}

	elf->shoff = EHDR(buf, elf->class, e_shoff);
	elf->shentsize = EHDR(buf, elf->class, e_shentsize);
	elf->shnum = EHDR(buf, elf->class, e_shnum);

	return true;
}

bool
parse_elf_sections(FILE *f, struct elf *elf, struct elf_section *sections[])
{
	uint32_t i, j;
	union elf_hdrbuf buf;

	assert(elf->shentsize <= sizeof(buf));
	struct elf_section tmp;

	if (!fs_fseek(f, elf->shoff)) {
		return false;
	}

	for (i = 0; i < elf->shnum; ++i) {
		if (!fs_fread(buf.bytes, elf->shentsize, f)) {
			return false;
		}

		tmp.type = SHDR(buf, elf->class, sh_type);
		tmp.off = SHDR(buf, elf->class, sh_offset);
		tmp.entsize = SHDR(buf, elf->class, sh_entsize);
		tmp.size = SHDR(buf, elf->class, sh_size);

		tmp.len = tmp.entsize ? tmp.size / tmp.entsize : 0;

		for (j = 0; sections[j]; ++j) {
			if (tmp.type != sections[j]->type) {
				continue;
			}

			*sections[j] = tmp;
			sections[j]->found = true;
			break;
		}

		bool all_found = true;
		for (j = 0; sections[j]; ++j) {
			all_found &= sections[j]->found;
		}

		if (all_found) {
			return true;
		}
	}

	return false;
}

static bool
parse_elf_dynamic(FILE *f, struct elf *elf, struct elf_section *s_dynamic, struct elf_dynstr *strs[])
{
	uint32_t i, j;
	union elf_hdrbuf buf;
	assert(s_dynamic->entsize <= sizeof(buf));
	struct elf_dynstr tmp;

	if (!fs_fseek(f, s_dynamic->off)) {
		return false;
	}

	for (i = 0; i < s_dynamic->len; ++i) {
		if (!fs_fread(buf.bytes, s_dynamic->entsize, f)) {
			return false;
		}

		tmp.tag = DHDR(buf, elf->class, d_tag);
		tmp.off = DHDR(buf, elf->class, d_un.d_val);

		for (j = 0; strs[j]; ++j) {
			if (tmp.tag != strs[j]->tag) {
				continue;
			}

			*strs[j] = tmp;
			strs[j]->found = true;
			strs[j]->index = i;
			break;
		}
	}

	return true;
}

static bool
remove_paths(struct workspace *wk, FILE *f, struct elf_section *s_dynstr, struct elf_dynstr *str, const char *build_root, bool *cleared)
{
	char rpath[BUF_SIZE_4k];
	uint32_t rpath_len = 0, cur_off = s_dynstr->off + str->off, copy_to = cur_off;
	bool modified = false, preserve_separator = false;
	*cleared = true;

	if (!fs_fseek(f, cur_off)) {
		return false;
	}

	for (;; ++cur_off) {
		char c;
		if (!fs_fread(&c, 1, f)) {
			return false;
		}

		if (!c || c == ':') {
			assert(rpath_len <= ARRAY_LEN(rpath));
			rpath[rpath_len] = 0;

			if (path_is_subpath(wk, build_root, rpath) || rpath_len == 0) {
				modified = true;
			} else {
				if (modified) {
					if (!fs_fseek(f, copy_to + (preserve_separator ? 1 : 0))) {
						return false;
					} else if (!fs_fwrite(rpath, rpath_len, f)) {
						return false;
					} else if (!fs_fwrite(&c, 1, f)) {
						return false;
					} else if (!fs_fseek(f, cur_off + 1)) {
						return false;
					}
				}
				copy_to += rpath_len;
				preserve_separator = c == ':';
				*cleared = false;
			}

			rpath_len = 0;
			if (!c) {
				break;
			} else if (c == ':') {
				continue;
			}
		}

		assert(rpath_len < ARRAY_LEN(rpath));
		rpath[rpath_len] = c;
		++rpath_len;
	}

	if (!fs_fseek(f, copy_to)) {
		return false;
	} else if (!fs_fwrite((char[]){ 0 }, 1, f)) {
		return false;
	}

	return true;
}

static bool
remove_path_entry(FILE *f, struct elf *elf, struct elf_section *s_dynamic, struct elf_dynstr *entry)
{
	char buf[BUF_SIZE_2k];
	assert(s_dynamic->entsize <= ARRAY_LEN(buf));

	uint32_t i;
	for (i = entry->index + 1; i < s_dynamic->len; ++i) {
		if (!fs_fseek(f, s_dynamic->off + (s_dynamic->entsize * i))) {
			return false;
		}

		if (!fs_fread(buf, s_dynamic->entsize, f)) {
			return false;
		}

		if (!fs_fseek(f, s_dynamic->off + (s_dynamic->entsize * (i - 1)))) {
			return false;
		}

		if (!fs_fwrite(buf, s_dynamic->entsize, f)) {
			return false;
		}
	}

	struct elf_dynstr mips_rld_map_rel = { .tag = DT_MIPS_RLD_MAP_REL };
	if (!parse_elf_dynamic(f, elf, s_dynamic, (struct elf_dynstr *[]){ &mips_rld_map_rel, NULL })) {
		return false;
	}

	if (mips_rld_map_rel.found) {
		LOG_W("TODO: fix mips_rld_map_rel");
	}

	return true;
}

bool
fix_rpaths(struct workspace *wk, const char *elf_path, const char *build_root)
{
	bool ret = false;
	FILE *f = NULL;
	if (!(f = fs_fopen(elf_path, "r+"))) {
		return false;
	}

	struct elf elf;
	if (!parse_elf(f, &elf)) {
		// the file is not an elf file
		ret = true;
		goto ret;
	}

	struct elf_section s_dynamic = { .type = SHT_DYNAMIC }, s_dynstr = { .type = SHT_STRTAB };
	if (!parse_elf_sections(f, &elf, (struct elf_section *[]){ &s_dynamic, &s_dynstr, NULL })) {
		goto ret;
	}

	struct elf_dynstr rpaths[] = { { .tag = DT_RPATH }, { .tag = DT_RUNPATH } };
	if (!parse_elf_dynamic(f, &elf, &s_dynamic, (struct elf_dynstr *[]){ &rpaths[0], &rpaths[1], NULL })) {
		goto ret;
	}

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(rpaths); ++i) {
		if (!rpaths[i].found) {
			continue;
		}

		bool cleared;
		if (!remove_paths(f, &s_dynstr, &rpaths[i], build_root, &cleared)) {
			goto ret;
		}

		if (cleared) {
			if (!remove_path_entry(f, &elf, &s_dynamic, &rpaths[i])) {
				goto ret;
			}
		}
	}

	ret = true;
ret:
	if (f) {
		if (!fs_fclose(f)) {
			return false;
		}
	}
	return ret;
}

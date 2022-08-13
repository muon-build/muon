#include "posix.h"

#include <assert.h>
#include <elf.h>
#include <string.h>

#include "buf_size.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/rpath_fixer.h"
#include "platform/uname.h"

#ifndef DT_MIPS_RLD_MAP_REL
#define DT_MIPS_RLD_MAP_REL 1879048245
#endif

enum elf_class {
	elf_class_32,
	elf_class_64
};

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

	uint8_t buf[BUF_SIZE_2k] = { 0 };

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
	default:
		return false;
	}

	switch (ident[EI_DATA]) {
	case ELFDATA2LSB:
		elf->endian = little_endian;
		break;
	case ELFDATA2MSB:
		elf->endian = big_endian;
		break;
	case ELFDATANONE:
	default:
		return false;
	}

	assert(hdr_size <= BUF_SIZE_2k);
	r = fread(&buf[EI_NIDENT], 1, hdr_size - EI_NIDENT, f);
	if (r != hdr_size - EI_NIDENT) {
		return false;
	}

	switch (elf->class) {
	case elf_class_32:
		elf->shoff = ((Elf32_Ehdr *)buf)->e_shoff;
		elf->shentsize = ((Elf32_Ehdr *)buf)->e_shentsize;
		elf->shnum = ((Elf32_Ehdr *)buf)->e_shnum;
		break;
	case elf_class_64:
		elf->shoff = ((Elf64_Ehdr *)buf)->e_shoff;
		elf->shentsize = ((Elf64_Ehdr *)buf)->e_shentsize;
		elf->shnum = ((Elf64_Ehdr *)buf)->e_shnum;
		break;
	}

	return true;
}

bool
parse_elf_sections(FILE *f, struct elf *elf, struct elf_section *sections[])
{
	uint32_t i, j;
	char buf[BUF_SIZE_2k];
	assert(elf->shentsize <= BUF_SIZE_2k);
	struct elf_section tmp;

	if (!fs_fseek(f, elf->shoff)) {
		return false;
	}

	for (i = 0; i < elf->shnum; ++i) {
		if (!fs_fread(buf, elf->shentsize, f)) {
			return false;
		}

		switch (elf->class) {
		case elf_class_32:
			tmp.type = ((Elf32_Shdr *)buf)->sh_type;
			tmp.off = ((Elf32_Shdr *)buf)->sh_offset;
			tmp.entsize = ((Elf32_Shdr *)buf)->sh_entsize;
			tmp.size = ((Elf32_Shdr *)buf)->sh_size;
			break;
		case elf_class_64:
			tmp.type = ((Elf64_Shdr *)buf)->sh_type;
			tmp.off = ((Elf64_Shdr *)buf)->sh_offset;
			tmp.entsize = ((Elf64_Shdr *)buf)->sh_entsize;
			tmp.size = ((Elf64_Shdr *)buf)->sh_size;
			break;
		}

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
	char buf[BUF_SIZE_2k];
	assert(s_dynamic->entsize <= BUF_SIZE_2k);
	struct elf_dynstr tmp;

	if (!fs_fseek(f, s_dynamic->off)) {
		return false;
	}

	for (i = 0; i < s_dynamic->len; ++i) {
		if (!fs_fread(buf, s_dynamic->entsize, f)) {
			return false;
		}

		switch (elf->class) {
		case elf_class_32:
			tmp.tag = ((Elf32_Dyn *)buf)->d_tag;
			tmp.off = ((Elf32_Dyn *)buf)->d_un.d_val;
			break;
		case elf_class_64:
			tmp.tag = ((Elf64_Dyn *)buf)->d_tag;
			tmp.off = ((Elf64_Dyn *)buf)->d_un.d_val;
			break;
		}

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
remove_paths(FILE *f, struct elf_section *s_dynstr, struct elf_dynstr *str, const char *build_root, bool *cleared)
{
	char rpath[PATH_MAX];
	uint32_t rpath_len = 0,
		 cur_off = s_dynstr->off + str->off,
		 copy_to = cur_off;
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
			assert(rpath_len <= PATH_MAX);
			rpath[rpath_len] = 0;

			if (path_is_subpath(build_root, rpath) || rpath_len == 0) {
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

		assert(rpath_len < PATH_MAX);
		rpath[rpath_len] = c;
		++rpath_len;
	}

	if (!fs_fseek(f, copy_to)) {
		return false;
	} else if (!fs_fwrite((char []) { 0 }, 1, f)) {
		return false;
	}

	return true;
}

static bool
remove_path_entry(FILE *f, struct elf *elf, struct elf_section *s_dynamic, struct elf_dynstr *entry)
{
	char buf[BUF_SIZE_2k];
	assert(s_dynamic->entsize <= BUF_SIZE_2k);

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
	if (!parse_elf_dynamic(f, elf, s_dynamic, (struct elf_dynstr *[]) { &mips_rld_map_rel, NULL })) {
		return false;
	}

	if (mips_rld_map_rel.found) {
		LOG_W("TODO: fix mips_rld_map_rel");
	}

	return true;
}

bool
fix_rpaths(const char *elf_path, const char *build_root)
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

	struct elf_section s_dynamic = { .type = SHT_DYNAMIC },
			   s_dynstr = { .type = SHT_STRTAB };
	if (!parse_elf_sections(f, &elf, (struct elf_section *[]) { &s_dynamic, &s_dynstr, NULL })) {
		goto ret;
	}

	struct elf_dynstr rpaths[] = { { .tag = DT_RPATH }, { .tag = DT_RUNPATH } };
	if (!parse_elf_dynamic(f, &elf, &s_dynamic, (struct elf_dynstr *[]) { &rpaths[0], &rpaths[1], NULL })) {
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

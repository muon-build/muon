#ifndef MUON_PLATFORM_RPATH_FIXER
#define MUON_PLATFORM_RPATH_FIXER

#include <stdbool.h>
#include <stdio.h>

bool fix_rpaths(const char *elf_path, const char *build_root);
#endif

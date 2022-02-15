#ifndef MUON_PLATFORM_RPATH_FIXER_H
#define MUON_PLATFORM_RPATH_FIXER_H

#include <stdbool.h>
#include <stdio.h>

bool fix_rpaths(const char *elf_path, const char *build_root);
#endif

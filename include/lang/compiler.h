/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_COMPILER_H
#define MUON_LANG_COMPILER_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/workspace.h"

enum vm_compile_mode {
	vm_compile_mode_fmt = 1 << 1,
	vm_compile_mode_quiet = 1 << 2,
	vm_compile_mode_language_extended = 1 << 3,
};

void vm_compile_initial_code_segment(struct workspace *wk);
bool vm_compile(struct workspace *wk, struct source *src, enum vm_compile_mode mode, uint32_t *entry);
#endif

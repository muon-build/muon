/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_INSTALL_H
#define MUON_INSTALL_H

#include "lang/workspace.h"

struct obj_install_target *push_install_target(struct workspace *wk, obj src,
	obj dest, obj mode);
bool push_install_target_install_dir(struct workspace *wk,
	obj src, obj install_dir, obj mode);
bool push_install_targets(struct workspace *wk, uint32_t err_node,
	obj filenames, obj install_dirs, obj install_mode, bool preserve_path);
#endif

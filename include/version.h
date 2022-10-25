/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Eli Schwartz <eschwartz@archlinux.org>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_VERSION_H
#define MUON_VERSION_H
struct muon_version {
	const char *const version, *const vcs_tag, *const meson_compat;
};
extern const struct muon_version muon_version;

#endif

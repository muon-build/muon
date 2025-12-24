/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_INIT_H
#define MUON_PLATFORM_INIT_H

typedef void((*platform_signal_handler_fn)(int signal, const char *signal_name, void *ctx));

void platform_init(void);
void platform_set_signal_handler(platform_signal_handler_fn handler, void *ctx);

#endif

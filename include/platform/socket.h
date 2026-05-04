/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_SOCKET_H
#define MUON_SOCKET_H

#include <stdbool.h>

struct socket_pair {
	int server, client;
};

bool socket_pair_create(const char *path, struct socket_pair *pair);
#endif

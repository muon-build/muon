/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_SOCKET_H
#define MUON_SOCKET_H

#if defined(_WIN32)
#include <windows.h>
#endif

#include <stdbool.h>
#include <stdint.h>

struct socket_server {
#if defined(_WIN32)
	HANDLE pipe;
	OVERLAPPED ol;
#else
	int server, client;
#endif
};

bool socket_server_create(const char *path, struct socket_server *pair);
struct tstr;
bool socket_server_read(struct socket_server *server, struct tstr *buf);
bool socket_server_write(struct socket_server *server, const char *buf, uint32_t len);
void socket_server_close(struct socket_server *server);
#endif

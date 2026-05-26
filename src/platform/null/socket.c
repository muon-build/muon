/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "log.h"
#include "platform/socket.h"

bool socket_server_create(const char *path, struct socket_server *pair)
{
	LOG_E("socket_server_create: not implemented");
	return false;
}

bool socket_server_read(struct socket_server *server, struct tstr *buf)
{
	LOG_E("socket_server_read: not implemented");
	return false;
}

bool socket_server_write(struct socket_server *server, const char *buf, uint32_t len)
{
	LOG_E("socket_server_write: not implemented");
	return false;
}

void socket_server_close(struct socket_server *server)
{
}

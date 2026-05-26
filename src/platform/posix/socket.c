/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "buf_size.h"
#include "lang/string.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/socket.h"

bool
socket_server_create(const char *path, struct socket_server *server)
{
	int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_fd == -1) {
		LOG_E("failed socket: %s", strerror(errno));
		return false;
	}

	union {
		struct sockaddr sa;
		struct sockaddr_un un;
	} addr;
	memset(&addr, 0, sizeof(addr));
	const struct str path_str = STRL(path);
	cstr_copy(addr.un.sun_path, &path_str);
	addr.un.sun_family = AF_UNIX;

	if (bind(server_fd, &addr.sa, sizeof(addr)) == -1) {
		LOG_E("failed bind: %s", strerror(errno));
		fs_close(&server_fd);
		return false;
	}

	if (listen(server_fd, 1) == -1) {
		LOG_E("failed listen: %s", strerror(errno));
		fs_close(&server_fd);
		return false;
	}

	LOG_I("waiting for client connection...");

	int client_fd = accept(server_fd, 0, 0);
	if (client_fd == -1) {
		LOG_E("failed accept: %s", strerror(errno));
		fs_close(&server_fd);
		return false;
	}

	server->server = server_fd;
	server->client = client_fd;
	return true;
}

bool
socket_server_read(struct socket_server *server, struct tstr *buf)
{
	const uint32_t space_available = buf->cap - buf->len;
	uint32_t bytes_available = space_available;
	if (!fs_wait_for_input(server->client, &bytes_available)) {
		return false;
	}

	if (bytes_available > space_available) {
		bytes_available = space_available;
	}

	int32_t n = fs_read(server->client, buf->buf + buf->len, bytes_available);
	if (n <= 0) {
		return false;
	}

	buf->len += n;

	return true;
}

bool
socket_server_write(struct socket_server *server, const char *buf, uint32_t len)
{
	return fs_write(server->client, buf, len);
}

void
socket_server_close(struct socket_server *server)
{
	close(server->server);
	close(server->client);
}

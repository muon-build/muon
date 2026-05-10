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
socket_pair_create(const char *path, struct socket_pair *pair)
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

	pair->server = server_fd;
	pair->client = client_fd;
	return true;
}

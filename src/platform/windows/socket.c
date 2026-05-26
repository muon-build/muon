/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdio.h>
#include <windows.h>

#include "buf_size.h"
#include "lang/string.h"
#include "log.h"
#include "platform/socket.h"
#include "platform/windows/win32_error.h"

bool
socket_server_create(const char *path, struct socket_server *server)
{
	*server = (struct socket_server){ 0 };

	server->ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	server->pipe = CreateNamedPipe(path,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		BUF_SIZE_16k,
		BUF_SIZE_16k,
		0,
		NULL);

	if (server->pipe == INVALID_HANDLE_VALUE) {
		LOG_E("CreateNamedPipe: %s", win32_error());
		return false;
	}

	LOG_I("waiting for client connection...");

	if (!ConnectNamedPipe(server->pipe, &server->ol)) {
		DWORD err = GetLastError();
		if (err == ERROR_IO_PENDING) {
			WaitForSingleObject(server->ol.hEvent, INFINITE);
		} else if (err != ERROR_PIPE_CONNECTED) {
			LOG_E("CreateNamedPipe: %s", win32_error());
			return false;
		}
	}
	return true;
}

bool
socket_server_read(struct socket_server *server, struct tstr *buf)
{
	const uint32_t space_available = buf->cap - buf->len;
	DWORD n = 0;

	bool ok = ReadFile(server->pipe, buf->buf, space_available, &n, &server->ol);

	if (!ok) {
		DWORD err = GetLastError();

		if (err == ERROR_IO_PENDING) {
			if (!GetOverlappedResult(server->pipe, &server->ol, &n, true)) {
				if (GetLastError() == ERROR_BROKEN_PIPE) {
					return false;
				}
			}
		} else if (err == ERROR_BROKEN_PIPE) {
			LOG_E("client closed connection");
			return false;
		} else {
			LOG_E("read failed with error: %s", win32_error());
			return false;
		}
	}

	buf->len += n;

	return true;
}

bool
socket_server_write(struct socket_server *server, const char *buf, uint32_t len)
{
	uint32_t written = 0;
	while (written < len) {
		DWORD n = 0;

		bool ok = WriteFile(server->pipe, buf, len, &n, &server->ol);
		if (!ok) {
			if (GetLastError() == ERROR_IO_PENDING) {
				if (!GetOverlappedResult(server->pipe, &server->ol, &n, true)) {
					LOG_E("GetOverlappedResult: %s", win32_error());
					return false;
				}
			} else {
				LOG_E("WriteFile: %s", win32_error());
				return false;
			}
		}

		written += n;
	}

	return true;
}

void
socket_server_close(struct socket_server *server)
{
	CloseHandle(server->ol.hEvent);
	CloseHandle(server->pipe);
}

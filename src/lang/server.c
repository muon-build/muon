/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdio.h>
#include <string.h>

#include "error.h"
#include "formats/json.h"
#include "lang/server.h"
#include "log.h"
#include "memmem.h"
#include "platform/assert.h"
#include "platform/socket.h"
#include "platform/filesystem.h"
#include "tracy.h"

struct stdio_server {
	int in, out;
};

static enum srv_read_result
srv_read_stdio(struct stdio_server *io, struct tstr *buf)
{
	const uint32_t space_available = buf->cap - buf->len;
	uint32_t bytes_available = space_available;
	if (!fs_wait_for_input(io->in, &bytes_available)) {
		return srv_read_result_err;
	}

	if (bytes_available > space_available) {
		bytes_available = space_available;
	}

	int32_t n = fs_read(io->in, buf->buf + buf->len, bytes_available);
	if (n <= 0) {
		return srv_read_result_eof;
	}

	buf->len += n;

	return srv_read_result_ok;
}

static bool
srv_write_stdio(struct stdio_server *io, const char *buf, uint32_t len)
{
	return fs_write(io->out, buf, len);
}

static enum srv_read_result
srv_read_bytes(struct server *srv)
{
	struct tstr *buf = &srv->in_buf;

	if (buf->cap - buf->len < 16) {
		tstr_grow(srv->wk, buf, 1024);
	}

	switch (srv->io_type) {
	case server_io_type_stdio: return srv_read_stdio(srv->io, buf);
	case server_io_type_pipe: {
		if (!socket_server_read(srv->io, buf)) {
			return srv_read_result_err;
		}
		break;
	}
	}

	return srv_read_result_ok;
}

static void
srv_buf_shift(struct server *srv, uint32_t amnt)
{
	struct tstr *buf = &srv->in_buf;

	char *start = buf->buf + amnt;
	buf->len = buf->len - (start - buf->buf);
	memmove(buf->buf, start, buf->len);
}

enum srv_read_result
srv_read(struct server *srv, struct workspace *wk, obj *msg)
{
	TracyCZoneAutoS;
	int64_t content_length = 0;
	struct tstr *buf = &srv->in_buf;

	{
		char *end;
		while (!(end = memmem(buf->buf, buf->len, "\r\n\r\n", 4))) {
			switch(srv_read_bytes(srv)) {
			case srv_read_result_err:
				LOG_E("error when reading message header");
				return srv_read_result_err;
			case srv_read_result_ok:
				break;
			case srv_read_result_eof:
				if (buf->len) {
					LOG_E("eof when reading message header");
					return srv_read_result_err;
				}
				return srv_read_result_eof;
			}
		}

		*(end + 2) = 0;

		const char *hdr = buf->buf;
		char *hdr_end;
		while ((hdr_end = strstr(hdr, "\r\n"))) {
			*hdr_end = 0;
			char *val;
			if ((val = strchr(buf->buf, ':'))) {
				*val = 0;
				val += 2;

				if (strcmp(hdr, "Content-Length") == 0) {
					if (!str_to_i(&STRL(val), &content_length, false)) {
						LOG_E("Invalid value for Content-Length");
					}
				} else if (strcmp(hdr, "Content-Type") == 0) {
					// Ignore
				} else {
					LOG_E("Unknown header: %s", hdr);
				}
			} else {
				LOG_E("Header missing ':': %s", hdr);
			}

			if (hdr_end == end) {
				break;
			}
		}

		if (!content_length) {
			LOG_E("Missing Content-Length header.");
			return srv_read_result_err;
		}

		srv_buf_shift(srv, (hdr_end - buf->buf) + 4);
	}

	{
		while (buf->len < content_length) {
			switch (srv_read_bytes(srv)) {
			case srv_read_result_err:
				LOG_E("error when reading message body");
				return srv_read_result_err;
			case srv_read_result_eof:
				LOG_E("eof when reading message body");
				return srv_read_result_err;
			case srv_read_result_ok:
				break;
			}
		}

		char end = buf->buf[content_length];
		buf->buf[content_length] = 0;
		const struct str json_msg = { .s = buf->buf, .len = content_length };
		if (!muon_json_to_obj(wk, &json_msg, msg)) {
			obj_lprintf(wk, log_error, "failed to parse json: %o", *msg);
			return srv_read_result_err;
		} else if (get_obj_type(wk, *msg) != obj_dict) {
			obj_lprintf(wk, log_error, "message was not a dict, got %s", obj_type_to_s(get_obj_type(wk, *msg)));
			return srv_read_result_err;
		}
		buf->buf[content_length] = end;

		srv_buf_shift(srv, content_length);
	}

	// obj_lprintf(wk, log_debug, "<<< %#o\n", *msg);

	TracyCZoneAutoE;
	return srv_read_result_ok;
}

void
srv_write(struct server *srv, struct workspace *wk, obj msg)
{
	// obj_lprintf(wk, log_debug, ">>> %#o\n", msg);
	TSTR(json_buf);
	if (!obj_to_json(wk, msg, &json_buf)) {
		UNREACHABLE;
	}

	TSTR(buf);
	tstr_pushf(wk, &buf, "Content-Length: %d\r\n\r\n%s", json_buf.len, json_buf.buf);

	switch (srv->io_type) {
	case server_io_type_stdio: {
		srv_write_stdio(srv->io, buf.buf, buf.len);
		break;
	}
	case server_io_type_pipe: {
		socket_server_write(srv->io, buf.buf, buf.len);
		break;
	}
	}
}

void
srv_init_stdio(struct workspace *wk, struct server *srv)
{
	*srv = (struct server){
		.io_type = server_io_type_pipe,
		.wk = wk,
	};

	struct stdio_server *io = ar_make(wk->a, struct stdio_server);
	srv->io = io;
	*io = (struct stdio_server){
		.in = 0, // STDIN_FILENO
		.out = 1, // STDOUT_FILENO
	};

	tstr_init(&srv->in_buf, 0);
}

bool
srv_init_pipe(struct workspace *wk, struct server *srv, const char *pipe_path)
{
	*srv = (struct server){
		.io_type = server_io_type_pipe,
		.wk = wk,
	};

	srv->io = ar_make(wk->a, struct socket_server);
	if (!socket_server_create(pipe_path, srv->io)) {
		return false;
	}

	tstr_init(&srv->in_buf, 0);
	return true;
}

void
srv_destroy(struct server *srv)
{
	switch (srv->io_type) {
	case server_io_type_stdio: break;
	case server_io_type_pipe: {
		socket_server_close(srv->io);
		break;
	}
	}
}

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_SERVER_H
#define MUON_LANG_SERVER_H

#include "lang/string.h"

enum srv_read_result {
	srv_read_result_err,
	srv_read_result_ok,
	srv_read_result_eof,
};

enum server_io_type {
	server_io_type_stdio,
	server_io_type_pipe,
};

struct server {
	struct workspace *wk;
	void *io;
	struct tstr in_buf;
	enum server_io_type io_type;
};


void srv_init_stdio(struct workspace *wk, struct server *srv);
bool srv_init_pipe(struct workspace *wk, struct server *srv, const char *pipe_path);
void srv_destroy(struct server *srv);
void srv_write(struct server *srv, struct workspace *wk, obj msg);
enum srv_read_result srv_read(struct server *srv, struct workspace *wk, obj *msg);
#endif

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

struct server {
	struct workspace *wk;
	struct tstr in_buf;
	int in, out, server;
};


void srv_init_stdio(struct workspace *wk, struct server *srv);
bool srv_init_pipe(struct workspace *wk, struct server *srv, const char *pipe_path);
void srv_destroy(struct server *srv);
void srv_write(struct server *srv, struct workspace *wk, obj msg);
enum srv_read_result srv_read(struct server *srv, struct workspace *wk, obj *msg);
#endif

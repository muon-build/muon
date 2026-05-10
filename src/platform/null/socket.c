/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "log.h"
#include "platform/socket.h"

bool
socket_pair_create(const char *path, struct socket_pair *pair)
{
	LOG_E("socket_pair_create: not implemented");
	return false;
}

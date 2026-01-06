/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#ifdef __NetBSD__
// for SA_RESETHAND
#define _NETBSD_SOURCE
#endif

#include <errno.h>
#include <signal.h>
#include <string.h>

#include "log.h"
#include "platform/init.h"

static struct {
	platform_signal_handler_fn handler;
	void *ctx;
} platform_signal_handler_ctx;

static void
platform_signal_handler(int signo, siginfo_t *info, void *_ctx)
{
	if (platform_signal_handler_ctx.handler) {
		const char *name = "unknown";
		switch (signo) {
		case SIGABRT: name = "abort"; break;
		case SIGSEGV: name = "segmentation fault"; break;
		case SIGBUS: name = "bus error"; break;
		}
		platform_signal_handler_ctx.handler(signo, name, platform_signal_handler_ctx.ctx);
	}
}

static void
platform_sigaction(int sig)
{
	struct sigaction act = {
		.sa_flags = SA_SIGINFO | SA_RESETHAND,
		.sa_sigaction = platform_signal_handler,
	};
	if (sigaction(sig, &act, 0) == -1) {
		LOG_W("failed to install signal handler: %s", strerror(errno));
	}
}

void
platform_init(void)
{
	platform_sigaction(SIGABRT);
	platform_sigaction(SIGSEGV);
	platform_sigaction(SIGBUS);
}

void
platform_set_signal_handler(platform_signal_handler_fn handler, void *ctx)
{
	platform_signal_handler_ctx.handler = handler;
	platform_signal_handler_ctx.ctx = ctx;
}

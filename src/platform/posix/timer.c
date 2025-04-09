/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "log.h"
#include "platform/timer.h"

void
timer_start(struct timer *t)
{
#ifdef CLOCK_MONOTONIC
	if (clock_gettime(CLOCK_MONOTONIC, &t->start) == -1) {
		LOG_E("clock_gettime: %s", strerror(errno));
	}
#else
	if (gettimeofday(&t->start, NULL) == -1) {
		LOG_E("gettimeofday: %s", strerror(errno));
	}
#endif
}

float
timer_read(struct timer *t)
{
#ifdef CLOCK_MONOTONIC
	struct timespec end;

	if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
		LOG_E("clock_gettime: %s", strerror(errno));
		return 0.0f;
	}

	double secs = (double)end.tv_sec - (double)t->start.tv_sec;
	double ns = ((secs * 1000000000.0) + end.tv_nsec) - t->start.tv_nsec;
	return (float)ns / 1000000000.0f;
#else
	struct timeval end;

	if (gettimeofday(&end, NULL) == -1) {
		LOG_E("gettimeofday: %s", strerror(errno));
		return 0.0f;
	}

	double secs = (double)end.tv_sec - (double)t->start.tv_sec;
	double us = ((secs * 1000000.0) + end.tv_usec) - t->start.tv_usec;
	return (float)us / 1000000.0f;
#endif
}

void
timer_sleep(uint64_t nanoseconds)
{
	struct timespec req = {
		.tv_nsec = nanoseconds,
	};
	nanosleep(&req, NULL);
}

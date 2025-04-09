/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_TIMER_H
#define MUON_PLATFORM_TIMER_H

#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

struct timer {
#if defined(_WIN32)
	LARGE_INTEGER freq;
	LARGE_INTEGER start;
#elif defined(CLOCK_MONOTONIC)
	struct timespec start;
#else
	struct timeval start;
#endif
};

void timer_start(struct timer *t);
float timer_read(struct timer *t);
void timer_sleep(uint64_t nanoseconds);

#endif

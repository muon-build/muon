/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "platform/timer.h"

void
timer_start(struct timer *t)
{
	QueryPerformanceFrequency(&t->freq);
	QueryPerformanceCounter(&t->start);
}

float
timer_read(struct timer *t)
{
	LARGE_INTEGER end;

	QueryPerformanceCounter(&end);
	return (float)(end.QuadPart - t->start.QuadPart) / (float)t->freq.QuadPart;
}

void
timer_sleep(uint64_t nanoseconds)
{
	Sleep(nanoseconds / 1000000ULL);
}

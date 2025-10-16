/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "platform/assert.h"
#include "platform/mem.h"
#include "tracy.h"

#ifdef TRACY_ENABLE
static const char *tracy_system_mem_tag = "system";
#endif

#if defined(TRACY_ENABLE) && !defined(_WIN32)
#include <sys/resource.h>

#define PlotRSS                         \
	struct rusage usage;            \
	getrusage(RUSAGE_SELF, &usage); \
	TracyCPlot("maxrss", ((double)usage.ru_maxrss / 1024.0));
#else
#define PlotRSS
#endif

void *
z_calloc(size_t nmemb, size_t size)
{
	TracyCZoneAutoS;
	assert(size);
	void *ret;
	ret = calloc(nmemb, size);

	if (!ret) {
		error_unrecoverable("calloc failed: %s", strerror(errno));
	}

	TracyCAllocN(ret, size * nmemb, tracy_system_mem_tag);
	/* PlotRSS; */
	TracyCZoneAutoE;
	return ret;
}

void *
z_malloc(size_t size)
{
	TracyCZoneAutoS;
	assert(size);
	void *ret;
	ret = malloc(size);

	if (!ret) {
		error_unrecoverable("malloc failed: %s", strerror(errno));
	}

	TracyCAllocN(ret, size, tracy_system_mem_tag);
	/* PlotRSS; */
	TracyCZoneAutoE;
	return ret;
}

void *
z_realloc(void *ptr, size_t size)
{
	TracyCZoneAutoS;
	assert(size);
	void *ret;
	TracyCFreeN(ptr, tracy_system_mem_tag);
	ret = realloc(ptr, size);

	if (!ret) {
		error_unrecoverable("realloc failed: %s", strerror(errno));
	}

	TracyCAllocN(ret, size, tracy_system_mem_tag);
	/* PlotRSS; */
	TracyCZoneAutoE;
	return ret;
}

void
z_free(void *ptr)
{
	TracyCZoneAutoS;
	assert(ptr);
	TracyCFreeN(ptr, tracy_system_mem_tag);
	free(ptr);
	/* PlotRSS; */
	TracyCZoneAutoE;
}

uint32_t
bswap_32(uint32_t x)
{
	return x >> 24 | (x >> 8 & 0xff00) | (x << 8 & 0xff0000) | x << 24;
}

#include "posix.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "platform/mem.h"
#include "tracy.h"

#ifdef TRACY_ENABLE
#include <sys/resource.h>

#define PlotRSS  \
	struct rusage usage; \
	getrusage(RUSAGE_SELF, &usage); \
	TracyCPlot("maxrss", ((double)usage.ru_maxrss / 1024.0));
#else
#define PlotRSS
#endif

void *
z_calloc(size_t nmemb, size_t size)
{
	assert(size);
	void *ret;
	ret = calloc(nmemb, size);

	if (!ret) {
		error_unrecoverable("calloc failed: %s", strerror(errno));
	}

	TracyCAlloc(ret, size * nmemb);
	PlotRSS;
	return ret;
}

void *
z_malloc(size_t size)
{
	assert(size);
	void *ret;
	ret = malloc(size);

	if (!ret) {
		error_unrecoverable("malloc failed: %s", strerror(errno));
	}

	TracyCAlloc(ret, size);
	PlotRSS;
	return ret;
}

void *
z_realloc(void *ptr, size_t size)
{
	assert(size);
	void *ret;
	ret = realloc(ptr, size);

	if (!ret) {
		error_unrecoverable("realloc failed: %s", strerror(errno));
	}

	TracyCFree(ptr);
	TracyCAlloc(ret, size);
	PlotRSS;
	return ret;
}

void
z_free(void *ptr)
{
	assert(ptr);
	free(ptr);
	TracyCFree(ptr);
	PlotRSS;
}

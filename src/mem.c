#include "posix.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "mem.h"

void *
z_calloc(size_t nmemb, size_t size)
{
	assert(size);
	void *ret;
	ret = calloc(nmemb, size);

	if (!ret) {
		error_unrecoverable("calloc failed: %s", strerror(errno));
	}
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

	return ret;
}

void
z_free(void *ptr)
{
	assert(ptr);
	free(ptr);
}

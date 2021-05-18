#include "posix.h"

#include <stdlib.h>
#include <assert.h>

#include "mem.h"

void *
z_calloc(size_t nmemb, size_t size)
{
	void *ret;
	ret = calloc(nmemb, size);
	assert(ret && "calloc failed");
	return ret;
}

void *
z_malloc(size_t size)
{
	void *ret;
	ret = malloc(size);
	assert(ret && "malloc failed");
	return ret;
}

void *
z_realloc(void *ptr, size_t size)
{
	void *ret;
	ret = realloc(ptr, size);
	assert(ret && "realloc failed");
	return ret;
}

void
z_free(void *ptr)
{
	assert(ptr);
	free(ptr);
}

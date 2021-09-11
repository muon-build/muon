#ifndef MUON_PLATFORM_MEM_H
#define MUON_PLATFORM_MEM_H
#include <stddef.h>

void *z_calloc(size_t nmemb, size_t size);
void *z_malloc(size_t size);
void *z_realloc(void *ptr, size_t size);
void z_free(void *ptr);
#endif

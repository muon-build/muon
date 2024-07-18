/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_MEM_H
#define MUON_PLATFORM_MEM_H
#include <stddef.h>
#include <stdint.h>

void *z_calloc(size_t nmemb, size_t size);
void *z_malloc(size_t size);
void *z_realloc(void *ptr, size_t size);
void z_free(void *ptr);

uint32_t bswap_32(uint32_t x);
#endif

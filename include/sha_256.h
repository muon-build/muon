/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_SHA_256_H
#define MUON_SHA_256_H

#include <stdint.h>
#include <stddef.h>

void calc_sha_256(uint8_t hash[32], const void *input, size_t len);
#endif

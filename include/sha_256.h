/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_SHA_256_H
#define MUON_SHA_256_H

#include <stddef.h>
#include <stdint.h>

void calc_sha_256(uint8_t hash[32], const void *input, size_t len);
void sha256_to_str(uint8_t hash[32], char str[65]);
#endif

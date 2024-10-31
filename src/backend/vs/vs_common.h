/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_VS_COMMON_H
#define MUON_VS_COMMON_H

#include "compat.h"

#define vs_guid_fmt "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"

struct guid
{
	uint32_t data1;
	uint16_t data2;
	uint16_t data3;
	uint8_t data4[8];
};

/* Debug and Release */
extern char *vs_configuration[2];

/* x64 x86, maybe later arm */
extern char *vs_platform[2];

#endif

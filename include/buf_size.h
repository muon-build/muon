/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_BUF_SIZE_H
#define MUON_BUF_SIZE_H

#define BUF_SIZE_S 255
#define BUF_SIZE_1k 1024
#define BUF_SIZE_2k 2048
#define BUF_SIZE_4k 4096
#define BUF_SIZE_16k (BUF_SIZE_1k * 16)
#define BUF_SIZE_32k (BUF_SIZE_1k * 32)
#define BUF_SIZE_1m 1048576ul

#define ARRAY_LEN(array) (sizeof(array) / sizeof(*array))
#endif

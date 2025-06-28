/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_EXTERNAL_SAMURAI_H
#define MUON_EXTERNAL_SAMURAI_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct samu_opts {
	FILE *out;
};

/* supported ninja version */
enum {
	samu_ninjamajor = 1,
	samu_ninjaminor = 9,
};

extern const bool have_samurai;

bool samu_main(int argc, char *argv[], struct samu_opts *opts);
#endif

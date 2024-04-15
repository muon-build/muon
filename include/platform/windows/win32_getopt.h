/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_WINDOWS_GETOPT_H
#define MUON_PLATFORM_WINDOWS_GETOPT_H
int getopt(int, char *const[], const char *);
extern char *optarg;
extern int optind, opterr, optopt;
#endif

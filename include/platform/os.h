/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_OS_H
#define MUON_PLATFORM_OS_H

#include <stdbool.h>

#ifdef _WIN32
extern char *optarg;
extern int opterr, optind, optopt;
#else
#include <unistd.h>
#endif

bool os_chdir(const char *path);
char *os_getcwd(char *buf, size_t size);
int os_getopt(int argc, char * const argv[], const char *optstring);

#endif

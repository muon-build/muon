/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_PLATFORM_OS_H
#define MUON_PLATFORM_OS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef S_IRUSR
#define S_IRUSR 0400
#endif
#ifndef S_IWUSR
#define S_IWUSR 0200
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
extern char *optarg;
extern int opterr, optind, optopt;
#else
#include <unistd.h>
#endif

#include "lang/object.h"

int os_getopt(int argc, char *const argv[], const char *optstring);

// Returns the number of jobs to spawn.  This number should be slightly larger
// than the number of cpus.
uint32_t os_parallel_job_count(void);

void os_set_env(const struct str *k, const struct str *v);
const char *os_get_env(const char *k);
bool os_is_debugger_attached(void);
int32_t os_get_pid(void);
#endif

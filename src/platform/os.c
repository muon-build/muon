/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "platform/os.h"

int32_t os_ncpus(void);

uint32_t
os_parallel_job_count(void)
{
	int32_t n = os_ncpus();

	if (n == -1) {
		return -1;
	} else if (n < 2) {
		return 2;
	} else {
		return n + 2;
	}
}

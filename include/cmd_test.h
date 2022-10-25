/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_CMD_TEST_H
#define MUON_CMD_TEST_H
#include <stdbool.h>
#include <stdint.h>

#include "lang/object.h"

enum test_flag {
	test_flag_should_fail = 1 << 0,
};

#define MAX_CMDLINE_TEST_SUITES 64

enum test_display {
	test_display_auto,
	test_display_dots,
	test_display_bar,
};

struct test_options {
	const char *suites[MAX_CMDLINE_TEST_SUITES];
	const char *setup;
	uint32_t suites_len, jobs, verbosity;
	enum test_display display;
	bool fail_fast, print_summary, no_rebuild, list;

	enum test_category cat;
};

bool tests_run(struct test_options *opts);
#endif

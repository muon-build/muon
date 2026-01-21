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

enum test_output {
	test_output_all,
	test_output_term,
	test_output_html,
	test_output_json,
};

struct test_options {
	const char *suites[MAX_CMDLINE_TEST_SUITES];
	char *const *tests;
	const char *setup;
	uint32_t suites_len, tests_len, jobs, verbosity;
	float timeout_multiplier;
	enum test_display display;
	enum test_output output;
	bool fail_fast, print_summary, no_rebuild, list, include_subprojects, build_only;

	enum test_category cat;
};

bool tests_run(struct workspace *wk, struct test_options *opts, const char *argv0);
#endif

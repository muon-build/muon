#ifndef MUON_TESTS_H
#define MUON_TESTS_H
#include <stdbool.h>
#include <stdint.h>

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
	uint32_t suites_len;
	enum test_display display;
};

bool tests_run(const char *build_dir, struct test_options *opts);
#endif

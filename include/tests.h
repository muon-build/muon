#ifndef MUON_TESTS_H
#define MUON_TESTS_H
#include <stdbool.h>
#include <stdint.h>

enum test_flag {
	test_flag_should_fail = 1 << 0,
};

bool tests_run(const char *build_root);
#endif


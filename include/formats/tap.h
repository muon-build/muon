#ifndef MUON_FORMATS_TAP_H
#define MUON_FORMATS_TAP_H

#include <stdbool.h>
#include <stdint.h>

struct tap_parse_result {
	uint32_t plan_count, pass, fail, skip;
	bool have_plan;
	bool bail_out;
	bool all_ok;
};

void tap_parse(char *buf, uint64_t buf_len, struct tap_parse_result *res);
#endif

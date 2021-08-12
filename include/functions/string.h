#ifndef FUNCTIONS_string_H
#define FUNCTIONS_string_H
#include "functions/common.h"

enum format_cb_result {
	format_cb_found,
	format_cb_not_found,
	format_cb_error,
	format_cb_skip,
};

struct version {
	uint32_t major, minor, patch;
};

typedef enum format_cb_result ((*string_format_cb)(struct workspace *wk, uint32_t node, void *ctx, const char *key, uint32_t *elem));

bool string_format(struct workspace *wk, uint32_t node, uint32_t str, uint32_t *out, void *ctx, string_format_cb cb);

extern const struct func_impl_name impl_tbl_string[];
#endif

#ifndef MUON_FUNCTIONS_STRING_H
#define MUON_FUNCTIONS_STRING_H
#include "functions/common.h"

enum format_cb_result {
	format_cb_found,
	format_cb_not_found,
	format_cb_error,
	format_cb_skip,
};

struct version {
	uint32_t v[3];
};
bool string_to_version(struct workspace *wk, struct version *v, const struct str *ss);
bool version_compare(struct workspace *wk, uint32_t err_node, const struct version *ver1, obj arr, bool *res);

typedef enum format_cb_result ((*string_format_cb)(struct workspace *wk, uint32_t node, void *ctx, const struct str *key, uint32_t *elem));

bool string_format(struct workspace *wk, uint32_t node, uint32_t str, uint32_t *out, void *ctx, string_format_cb cb);

extern const struct func_impl_name impl_tbl_string[];
#endif

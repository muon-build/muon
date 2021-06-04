#ifndef BOSON_COERCE_H
#define BOSON_COERCE_H
#include "functions/common.h"

enum requirement_type {
	requirement_skip,
	requirement_required,
	requirement_auto,
};

bool coerce_requirement(struct workspace *wk, struct args_kw *kw_required, enum requirement_type *requirement);
bool coerce_files(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res);
bool coerce_dirs(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res);
bool coerce_output_files(struct workspace *wk, uint32_t node, uint32_t val, uint32_t *res);
#endif

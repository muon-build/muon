#ifndef MUON_FUNCTIONS_SUBPROJECT_H
#define MUON_FUNCTIONS_SUBPROJECT_H
#include "functions/common.h"

bool subproject_get_variable(struct workspace *wk, uint32_t node, uint32_t name_id,
	uint32_t subproj, uint32_t *obj);

extern const struct func_impl_name impl_tbl_subproject[];
#endif

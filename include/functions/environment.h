#ifndef MUON_FUNCTIONS_ENVIRONMENT_H
#define MUON_FUNCTIONS_ENVIRONMENT_H
#include "functions/common.h"

bool typecheck_environment_dict(struct workspace *wk, uint32_t err_node, obj dict);
extern const struct func_impl_name impl_tbl_environment[];
#endif

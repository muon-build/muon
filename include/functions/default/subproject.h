#ifndef MUON_FUNCTIONS_DEFAULT_SUBPROJECT_H
#define MUON_FUNCTIONS_DEFAULT_SUBPROJECT_H

#include "coerce.h"
#include "functions/common.h"
#include "lang/workspace.h"

bool subproject(struct workspace *wk, obj name, enum requirement_type req, struct args_kw *default_options, obj *res);
bool func_subproject(struct workspace *wk, obj _, uint32_t args_node, obj *res);
#endif

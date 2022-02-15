#ifndef MUON_FUNCTIONS_DEFAULT_DEPENDENCY_H
#define MUON_FUNCTIONS_DEFAULT_DEPENDENCY_H
#include "functions/common.h"

bool func_dependency(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res);
bool func_declare_dependency(struct workspace *wk, obj _, uint32_t args_node, obj *res);
#endif

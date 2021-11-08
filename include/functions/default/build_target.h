#ifndef MUON_FUNCTIONS_DEFAULT_BUILD_TARGET_H
#define MUON_FUNCTIONS_DEFAULT_BUILD_TARGET_H
#include "functions/common.h"

bool func_executable(struct workspace *wk, obj _, uint32_t args_node, obj *res);
bool func_static_library(struct workspace *wk, obj _, uint32_t args_node, obj *res);
bool func_build_target(struct workspace *wk, obj _, uint32_t args_node, obj *res);
#endif

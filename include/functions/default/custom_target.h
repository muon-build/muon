#ifndef MUON_FUNCTIONS_DEFAULT_CUSTOM_TARGET_H
#define MUON_FUNCTIONS_DEFAULT_CUSTOM_TARGET_H
#include "functions/common.h"

bool process_custom_target_commandline(struct workspace *wk, uint32_t err_node,
	obj arr, obj input, obj output, obj depfile, obj depends, obj *res);
bool func_custom_target(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj);
#endif

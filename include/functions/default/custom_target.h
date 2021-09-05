#ifndef MUON_FUNCTIONS_DEFAULT_CUSTOM_TARGET_H
#define MUON_FUNCTIONS_DEFAULT_CUSTOM_TARGET_H
#include "functions/common.h"

bool process_custom_target_commandline(struct workspace *wk, uint32_t err_node,
	uint32_t arr, uint32_t input, uint32_t output, uint32_t depfile,
	uint32_t *res);
bool func_custom_target(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj);
#endif

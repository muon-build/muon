#ifndef BOSON_FUNCTIONS_DEFAULT_DEPENDENCY_H
#define BOSON_FUNCTIONS_DEFAULT_DEPENDENCY_H

#include "functions/common.h"
#include "run_cmd.h"

bool func_dependency(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj);
bool pkg_config(struct workspace *wk, struct run_cmd_ctx *ctx, uint32_t args_node, const char *arg, const char *depname);
#endif

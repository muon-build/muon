#ifndef MUON_FUNCTIONS_KERNEL_OPTIONS_H
#define MUON_FUNCTIONS_KERNEL_OPTIONS_H

#include "lang/workspace.h"

bool func_option(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res);
bool func_get_option(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res);
#endif

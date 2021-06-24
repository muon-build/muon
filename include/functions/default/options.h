#ifndef MUON_FUNCTIONS_DEFAULT_OPTIONS_H
#define MUON_FUNCTIONS_DEFAULT_OPTIONS_H

#include "workspace.h"

bool func_option(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj);
bool func_get_option(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj);
bool check_invalid_option_overrides(struct workspace *wk);
bool check_invalid_subproject_option(struct workspace *wk);
void set_default_options(struct workspace *wk);
#endif

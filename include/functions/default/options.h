#ifndef MUON_FUNCTIONS_DEFAULT_OPTIONS_H
#define MUON_FUNCTIONS_DEFAULT_OPTIONS_H

#include "lang/workspace.h"

bool func_option(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj);
bool func_get_option(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj);
void get_option(struct workspace *wk, const struct project *proj, const char *name, uint32_t *obj);
bool check_invalid_option_overrides(struct workspace *wk);
bool check_invalid_subproject_option(struct workspace *wk);
bool set_builtin_options(struct workspace *wk);

bool parse_and_set_cmdline_option(struct workspace *wk, char *lhs);
bool parse_and_set_default_options(struct workspace *wk, uint32_t err_node, obj arr, str project_name);
#endif

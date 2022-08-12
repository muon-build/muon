#ifndef MUON_FUNCTIONS_KERNEL_INSTALL_H
#define MUON_FUNCTIONS_KERNEL_INSTALL_H
#include "functions/common.h"

bool func_install_subdir(struct workspace *wk, obj _, uint32_t args_node, obj *ret);
bool func_install_man(struct workspace *wk, obj _, uint32_t args_node, obj *ret);
bool func_install_symlink(struct workspace *wk, obj _, uint32_t args_node, obj *ret);
bool func_install_emptydir(struct workspace *wk, obj _, uint32_t args_node, obj *ret);
bool func_install_data(struct workspace *wk, obj _, uint32_t args_node, obj *res);
bool func_install_headers(struct workspace *wk, obj _, uint32_t args_node, obj *ret);
#endif

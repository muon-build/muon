#ifndef MUON_FUNCTIONS_MODULES_H
#define MUON_FUNCTIONS_MODULES_H

#include "functions/common.h"

extern const char *module_names[module_count];
extern const struct func_impl_name impl_tbl_module[];
extern const struct func_impl_name *module_func_tbl[module_count][language_mode_count];

bool module_lookup(const char *name, enum module *res, bool *has_impl);
const struct func_impl_name *module_func_lookup(struct workspace *wk, const char *name, enum module mod);
#endif

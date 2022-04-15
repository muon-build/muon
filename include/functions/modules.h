#ifndef MUON_FUNCTIONS_MODULES_H
#define MUON_FUNCTIONS_MODULES_H

#include "functions/common.h"

extern const char *module_names[module_count];

bool module_lookup(const char *name, enum module *res);
const struct func_impl_name *module_func_lookup(const char *name, enum module mod);
#endif

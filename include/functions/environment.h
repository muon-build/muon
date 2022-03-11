#ifndef MUON_FUNCTIONS_ENVIRONMENT_H
#define MUON_FUNCTIONS_ENVIRONMENT_H
#include "functions/common.h"

void set_default_environment_vars(struct workspace *wk, obj env, bool set_subdir);
extern const struct func_impl_name impl_tbl_environment[];
#endif

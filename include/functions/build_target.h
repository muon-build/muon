#ifndef MUON_FUNCTIONS_BUILD_TARGET_H
#define MUON_FUNCTIONS_BUILD_TARGET_H
#include "functions/common.h"

bool tgt_build_path(struct workspace *wk, struct obj *tgt, bool relative, char res[PATH_MAX]);

extern const struct func_impl_name impl_tbl_build_target[];
#endif

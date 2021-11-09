#ifndef MUON_FUNCTIONS_BUILD_TARGET_H
#define MUON_FUNCTIONS_BUILD_TARGET_H
#include "functions/common.h"

bool tgt_build_path(struct workspace *wk, const struct obj *tgt, bool relative, char res[PATH_MAX]);
bool tgt_parts_dir(struct workspace *wk, const struct obj *tgt, bool relative, char res[PATH_MAX]);
bool tgt_src_to_object_path(struct workspace *wk, const struct obj *tgt, obj src_file, bool relative, char res[PATH_MAX]);

bool build_target_extract_all_objects(struct workspace *wk, uint32_t err_node, obj rcvr, obj *res);

extern const struct func_impl_name impl_tbl_build_target[];
#endif

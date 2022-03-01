#ifndef MUON_FUNCTIONS_GENERATOR_H
#define MUON_FUNCTIONS_GENERATOR_H
#include "functions/common.h"

bool generated_list_process_for_target(struct workspace *wk, uint32_t err_node,
	obj gl, struct obj_build_target *tgt, bool add_targets, obj *res);

extern const struct func_impl_name impl_tbl_generator[];
#endif

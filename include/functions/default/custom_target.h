#ifndef MUON_FUNCTIONS_DEFAULT_CUSTOM_TARGET_H
#define MUON_FUNCTIONS_DEFAULT_CUSTOM_TARGET_H
#include "functions/common.h"

bool make_custom_target(struct workspace *wk,
	obj name,
	uint32_t input_node,
	uint32_t output_node,
	uint32_t command_node,
	obj input_orig,
	obj output_orig,
	const char *output_dir,
	obj command_orig,
	obj depfile_orig,
	bool capture,
	obj *res);

bool process_custom_target_commandline(struct workspace *wk, uint32_t err_node, bool relativize,
	obj name, obj arr, obj input, obj output, obj depfile, obj depends,
	const char *output_dir, obj *res);

bool func_custom_target(struct workspace *wk, obj _, uint32_t args_node, obj *res);

bool func_vcs_tag(struct workspace *wk, obj _, uint32_t args_node, obj *res);
#endif

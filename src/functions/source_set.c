#include "posix.h"

#include "functions/common.h"
#include "functions/source_set.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
func_source_set_add(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	const type_tag tc_ss_sources = tc_string | tc_file | tc_custom_target
				       | tc_generated_list;

	struct args_norm an[] = { { ARG_TYPE_GLOB | tc_ss_sources | tc_dependency },
				  ARG_TYPE_NULL };
	enum kwargs {
		kw_when,
		kw_if_true,
		kw_if_false,
	};
	struct args_kw akw[] = {
		[kw_when] = { "when", ARG_TYPE_ARRAY_OF | tc_string | tc_dependency },
		[kw_if_true] = { "if_true", ARG_TYPE_ARRAY_OF | tc_ss_sources | tc_dependency },
		[kw_if_false] = { "if_false", ARG_TYPE_ARRAY_OF | tc_ss_sources  },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}


	return true;
}

static bool
func_source_set_add_all(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { ARG_TYPE_GLOB | tc_source_set }, ARG_TYPE_NULL };
	enum kwargs {
		kw_when,
		kw_if_true,
	};
	struct args_kw akw[] = {
		[kw_when] = { "when", ARG_TYPE_ARRAY_OF | tc_string | tc_dependency },
		[kw_if_true] = { "if_true", ARG_TYPE_ARRAY_OF | tc_source_set },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	return true;
}

static bool
func_source_set_all_sources(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_array);
	return true;
}

static bool
func_source_set_all_dependencies(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, res, obj_array);
	return true;
}

static bool
func_source_set_apply(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { tc_configuration_data | tc_dict }, ARG_TYPE_NULL };
	enum kwargs {
		kw_strict,
	};
	struct args_kw akw[] = {
		[kw_strict] = { "strict", tc_bool },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	return true;
}

const struct func_impl_name impl_tbl_source_set[] = {
	{ "add", func_source_set_add, 0, true },
	{ "add_all", func_source_set_add_all, 0, true },
	{ "all_sources", func_source_set_all_sources, tc_array, true },
	{ "all_dependencies", func_source_set_all_dependencies, tc_array, true },
	{ "apply", func_source_set_apply, tc_source_configuration, true },
	{ NULL, NULL },
};

#include "posix.h"

#include "functions/common.h"
#include "functions/feature_opt.h"
#include "lang/interpreter.h"
#include "log.h"

static bool
feature_opt_common(struct workspace *wk, uint32_t rcvr, uint32_t args_node,
	uint32_t *obj, enum feature_opt_state state)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_bool)->dat.boolean =
		get_obj(wk, rcvr)->dat.feature_opt.state == state;
	return true;
}

static bool
func_feature_opt_auto(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	return feature_opt_common(wk, rcvr, args_node, obj, feature_opt_auto);
}

static bool
func_feature_opt_disabled(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	return feature_opt_common(wk, rcvr, args_node, obj, feature_opt_disabled);
}

static bool
func_feature_opt_enabled(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	return feature_opt_common(wk, rcvr, args_node, obj, feature_opt_enabled);
}

const struct func_impl_name impl_tbl_feature_opt[] = {
	{ "auto", func_feature_opt_auto },
	{ "disabled", func_feature_opt_disabled },
	{ "enabled", func_feature_opt_enabled },
	{ NULL, NULL },
};

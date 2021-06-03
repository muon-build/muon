#include "posix.h"

#include "functions/common.h"
#include "functions/string.h"
#include "interpreter.h"
#include "log.h"

static bool
func_strip(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push_stripped(wk, wk_objstr(wk, rcvr));
	return true;
}

static bool
func_to_upper(struct workspace *wk, uint32_t rcvr, uint32_t args_node, uint32_t *obj)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_push(wk, wk_objstr(wk, rcvr));

	char *s = wk_objstr(wk, *obj);

	for (; *s; ++s) {
		if ('a' <= *s && *s <= 'z') {
			*s -= 32;
		}
	}

	return true;
}

const struct func_impl_name impl_tbl_string[] = {
	{ "strip", func_strip },
	{ "to_upper", func_to_upper },
	{ NULL, NULL },
};

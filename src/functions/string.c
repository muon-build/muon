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

	const char *s = wk_objstr(wk, rcvr), *start;
	uint32_t len = 0;

	while (*s && (*s == ' ' || *s == '\n')) {
		++s;
	}

	start = s;

	while (*s && !(*s == ' ' || *s == '\n')) {
		++s;
		++len;
	}

	make_obj(wk, obj, obj_string)->dat.str = wk_str_pushn(wk, start, len);
	return true;
}

const struct func_impl_name impl_tbl_string[] = {
	{ "strip", func_strip },
	{ NULL, NULL },
};

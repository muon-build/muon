#include "posix.h"

#include "functions/common.h"
#include "functions/number.h"
#include "interpreter.h"
#include "log.h"

static bool
func_number_to_string(struct ast *ast, struct workspace *wk, uint32_t rcvr, struct node *args, uint32_t *obj)
{
	if (!interp_args(ast, wk, args, NULL, NULL, NULL)) {
		return false;
	}

	struct obj *str = make_obj(wk, obj, obj_string);
	str->dat.str = wk_str_pushf(wk, "%ld", get_obj(wk, rcvr)->dat.num);

	return true;
}

const struct func_impl_name impl_tbl_number[] = {
	{ "to_string", func_number_to_string },
	{ NULL, NULL },
};

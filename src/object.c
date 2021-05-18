#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "interpreter.h"
#include "log.h"
#include "object.h"
#include "parser.h"

const char *
obj_type_to_s(enum obj_type t)
{
	switch (t) {
	case obj_none: return "none";
	case obj_any: return "any";
	case obj_compiler: return "compiler";
	case obj_meson: return "meson";
	case obj_string: return "string";
	case obj_array: return "array";
	case obj_bool: return "bool";
	case obj_file: return "file";
	case obj_type_count: return "type_count";
	}

	assert(false && "unreachable");
	return NULL;
}

bool
obj_array_foreach(struct workspace *wk, uint32_t arr_id, void *ctx, obj_array_iterator cb)
{
	struct obj *arr_elem = get_obj(wk, arr_id);

	while (true) {
		switch (cb(wk, ctx, arr_elem->dat.arr.l)) {
		case ir_cont:
			break;
		case ir_done:
			return true;
		case ir_err:
			return false;
		}

		if (!arr_elem->dat.arr.have_r) {
			break;
		}
		arr_elem = get_obj(wk, arr_elem->dat.arr.r);
	}

	return true;
}

void
obj_array_push(struct workspace *wk, uint32_t arr_id, uint32_t child_id)
{
	uint32_t child_arr_id;
	struct obj *arr, *tail, *child_arr;

	if (!(arr = get_obj(wk, arr_id))->dat.arr.len) {
		arr->dat.arr.tail = arr_id;
		arr->dat.arr.len = 1;
		arr->dat.arr.l = child_id;
		return;
	}

	child_arr = make_obj(wk, &child_arr_id, obj_array);
	child_arr->dat.arr.l = child_id;

	arr = get_obj(wk, arr_id);
	assert(arr->type == obj_array);

	tail = get_obj(wk, arr->dat.arr.tail);
	assert(tail->type == obj_array);
	assert(!tail->dat.arr.have_r);
	tail->dat.arr.have_r = true;
	tail->dat.arr.r = child_arr_id;

	arr->dat.arr.tail = child_arr_id;
	++arr->dat.arr.len;
}

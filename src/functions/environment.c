#include "posix.h"

#include "functions/common.h"
#include "functions/environment.h"
#include "lang/interpreter.h"
#include "log.h"

enum environment_set_mode {
	environment_set_mode_set,
	environment_set_mode_append,
	environment_set_mode_prepend,
};

static bool
environment_set_common(struct workspace *wk, obj rcvr, uint32_t args_node, enum environment_set_mode mode)
{
	struct args_norm an[] = { { obj_string }, { ARG_TYPE_GLOB }, ARG_TYPE_NULL };
	enum kwargs {
		kw_separator,
	};
	struct args_kw akw[] = {
		[kw_separator] = { "separator", obj_string },
		0
	};
	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	obj dict = get_obj_environment(wk, rcvr)->env,
	    key = an[0].val,
	    value = an[1].val,
	    sep;

	if (akw[kw_separator].set) {
		sep = akw[kw_separator].val;
	} else {
		sep = make_str(wk, ":");
	}

	if (!get_obj_array(wk, value)->len) {
		interp_error(wk, an[0].node, "you must pass at least one value");
		return false;
	}

	obj joined;
	if (!obj_array_join(wk, false, value, sep, &joined)) {
		return false;
	}

	obj orig;
	if (!obj_dict_index(wk, dict, key, &orig)) {
		obj_dict_set(wk, dict, key, joined);
		return true;
	}

	obj head, tail;

	switch (mode) {
	case environment_set_mode_set:
		obj_dict_set(wk, dict, key, joined);
		return true;
	case environment_set_mode_append:
		head = orig;
		tail = joined;
		break;
	case environment_set_mode_prepend:
		head = joined;
		tail = orig;
		break;
	default:
		head = 0; tail = 0;
		assert(false && "unreachable");
		return false;
	}

	obj new_val =
		make_strf(wk, "%s%s%s", get_cstr(wk, head), get_cstr(wk, sep), get_cstr(wk, tail));

	obj_dict_set(wk, dict, key, new_val);
	return true;
}

static bool
func_environment_set(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return environment_set_common(wk, rcvr, args_node, environment_set_mode_set);
}

static bool
func_environment_append(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return environment_set_common(wk, rcvr, args_node, environment_set_mode_append);
}

static bool
func_environment_prepend(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	return environment_set_common(wk, rcvr, args_node, environment_set_mode_prepend);
}

const struct func_impl_name impl_tbl_environment[] = {
	{ "set", func_environment_set },
	{ "append", func_environment_append },
	{ "prepend", func_environment_prepend },
	{ NULL, NULL },
};

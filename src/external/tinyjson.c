/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Seedo Paul <seedoeldhopaul@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <tiny-json.h>

#include "lang/object.h"
#include "lang/string.h"
#include "log.h"

#define MAX_FIELDS 1024

static bool
build_dict_from_json(struct workspace *wk, const json_t *json, obj *res)
{
	switch (json_getType(json)) {
	case JSON_OBJ:
		make_obj(wk, res, obj_dict);
		for (const json_t *child = json_getChild(json); child; child = json_getSibling(child)) {
			obj val;

			if (!build_dict_from_json(wk, child, &val)) {
				return false;
			}

			obj key = make_str(wk, json_getName(child));
			obj_dict_set(wk, *res, key, val);
		}
		break;
	case JSON_ARRAY:
		make_obj(wk, res, obj_array);
		for (const json_t *child = json_getChild(json); child; child = json_getSibling(child)) {
			obj val;

			if (!build_dict_from_json(wk, child, &val)) {
				return false;
			}

			obj_array_push(wk, *res, val);
		}
		break;
	case JSON_TEXT:
	/* muon doesn't have reals, so use string for the time being */
	case JSON_REAL:
		*res = make_str(wk, json_getValue(json));
		break;
	case JSON_INTEGER:
		make_obj(wk, res, obj_number);
		set_obj_number(wk, *res, (int64_t)json_getInteger(json));
		break;
	case JSON_NULL:
		*res = obj_null;
		break;
	case JSON_BOOLEAN:
		make_obj(wk, res, obj_bool);
		set_obj_bool(wk, *res, json_getBoolean(json));
		break;
	default:
		LOG_E("error parsing json: invalid object");
		return false;
	}

	return true;
}

bool
muon_json_to_dict(struct workspace *wk, char *json_str, obj *res)
{
	json_t mem[MAX_FIELDS];

	const json_t *json = json_create(json_str, mem, MAX_FIELDS);
	if (!json) {
		LOG_E("error parsing json to obj_dict: syntax error or out of memory");
		return false;
	}

	if (json_getType(json) != JSON_OBJ) {
		LOG_E("error parsing json to obj_dict: unexpected or invalid object");
		return false;
	}

	return build_dict_from_json(wk, json, res);
}

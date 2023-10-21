/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "embedded.h"
#include "json.h"
#include "log.h"

#include "functions/modules/python.h"
#include "functions/external_program.h"
#include "lang/interpreter.h"
#include "platform/filesystem.h"
#include "platform/run_cmd.h"

static inline bool json_streq(const char *buf, jsmntok_t *token,
	const char *needle)
{
	if (token->type != JSMN_STRING)
		return false;

	return strncmp(buf + token->start, needle, token->end - token->start) == 0;
}

static jsmntok_t *parse_json_string(struct workspace *wk, obj *res,
	const char *buffer, jsmntok_t *tok)
{
	assert(tok->type == JSMN_STRING);

	*res = make_strn(wk, buffer + tok->start, tok->end - tok->start);
	return tok + 1;
}

static jsmntok_t *parse_json_primitive(struct workspace *wk, obj *res,
	const char *buffer, jsmntok_t *tok)
{
	assert(tok->type == JSMN_PRIMITIVE);

	char first_char = buffer[tok->start];
	switch(first_char) {
		case 't':
		case 'f':
			make_obj(wk, res, obj_bool);
			set_obj_bool(wk, *res, (first_char == 't'));
			break;

		default: {
			char *end = ((char *)buffer) + tok->end;
			int64_t value = strtoll(buffer + tok->start, &end, 10);
			make_obj(wk, res, obj_number);
			set_obj_number(wk, *res, value);
			break;
		}
	}

	return tok + 1;
}

static jsmntok_t *parse_json_object(struct workspace *wk, obj *res,
	const char *buffer, jsmntok_t *tok)
{
	assert(tok->type == JSMN_STRING || tok->type == JSMN_PRIMITIVE);

	switch(tok->type) {
		case JSMN_STRING:
			return parse_json_string(wk, res, buffer, tok);
		case JSMN_PRIMITIVE:
			return parse_json_primitive(wk, res, buffer, tok);
		default:
			return NULL;
	}
}

static jsmntok_t *parse_json_dict(struct workspace *wk, obj *res,
	const char *buffer, jsmntok_t *tok)
{
	jsmntok_t *cur_tok;

	assert(res != NULL);
	assert(tok->type == JSMN_OBJECT);

	make_obj(wk, res, obj_dict);

	cur_tok = tok + 1;

	for(int i = 0; i < tok->size; ++i) {

		/* Get the name. */
		obj name_obj;
		cur_tok = parse_json_string(wk, &name_obj, buffer, cur_tok);
		if (!name_obj)
			continue;

		/* Get the value. */
		obj value_obj;
		cur_tok = parse_json_object(wk, &value_obj, buffer, cur_tok);
		if (!value_obj)
			continue;

		obj_dict_set(wk, *res, name_obj, value_obj);
	}

	return cur_tok;
}

static bool
introspect_python_interpreter(struct workspace *wk, const char *path,
	struct obj_python_installation *python)
{
	struct run_cmd_ctx cmd_ctx = { 0 };
	const char *python_info_str = NULL;
	jsmn_parser parser;
	jsmntok_t *tokens = NULL;
	size_t num_tokens = 2048;
	int32_t parsed_tokens = 0;

	python_info_str = embedded_get("python_info.py");
	if (python_info_str == NULL) {
		return false;
	}

	char *const args[] = { (char *)path, "-c", (char *)python_info_str, 0 };
	if (!run_cmd_argv(&cmd_ctx, args, NULL, 0) || cmd_ctx.status != 0) {
		return false;
	}

	tokens = calloc(num_tokens, sizeof(jsmntok_t));
	if (tokens == NULL) {
		return false;
	}

	jsmn_init(&parser);

	char *buf = cmd_ctx.out.buf;
	parsed_tokens = jsmn_parse(&parser, buf, cmd_ctx.out.len, tokens, num_tokens);
	if (parsed_tokens < 0) {
		LOG_E("failed to parse python json info\n");
		return false;
	}

	jsmntok_t root_token = tokens[0];
	jsmntok_t *cur_tok = &tokens[1];

	for (int i = 0; i < root_token.size; ++i) {

		if (json_streq(buf, cur_tok, "variables")) {
			++cur_tok;
			cur_tok = parse_json_dict(wk, &python->info.variables, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "paths")) {
			++cur_tok;
			cur_tok = parse_json_dict(wk, &python->info.paths, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "sysconfig_paths")) {
			++cur_tok;
			cur_tok = parse_json_dict(wk, &python->info.sysconfig_paths, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "install_paths")) {
			++cur_tok;
			cur_tok = parse_json_dict(wk, &python->info.install_paths, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "version")) {
			++cur_tok;
			cur_tok = parse_json_string(wk, &python->info.language_version, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "platform")) {
			++cur_tok;
			cur_tok = parse_json_string(wk, &python->info.platform, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "is_pypy")) {
			++cur_tok;
			cur_tok = parse_json_primitive(wk, &python->info.is_pypy, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "is_venv")) {
			++cur_tok;
			cur_tok = parse_json_primitive(wk, &python->info.is_venv, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "link_libpython")) {
			++cur_tok;
			cur_tok = parse_json_primitive(wk, &python->info.link_libpython, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "suffix")) {
			++cur_tok;
			cur_tok = parse_json_string(wk, &python->info.suffix, buf, cur_tok);
		} else if (json_streq(buf, cur_tok, "limited_api_suffix")) {
			++cur_tok;
			cur_tok = parse_json_string(wk, &python->info.limited_api_suffix, buf, cur_tok);
		}
	}

	bool success = (cur_tok != NULL);

	run_cmd_ctx_destroy(&cmd_ctx);
	free(tokens);

	return success;
}

static bool
python_module_present(struct workspace *wk, const char *pythonpath, const char *mod)
{
	struct run_cmd_ctx cmd_ctx = { 0 };

	SBUF(importstr);
	sbuf_pushf(wk, &importstr, "import %s", mod);

	char *const *args = (char *const []){
		(char *)pythonpath,
		"-c",
		importstr.buf,
		0
	};

	bool present = run_cmd_argv(&cmd_ctx, args, NULL, 0) && cmd_ctx.status == 0;

	run_cmd_ctx_destroy(&cmd_ctx);

	return present;
}

struct iter_mod_ctx {
	const char *pythonpath;
	uint32_t node;
	bool required;
};

static enum iteration_result
iterate_required_module_list(struct workspace *wk, void *ctx, obj val)
{
	struct iter_mod_ctx *_ctx = ctx;
	const char *mod = get_cstr(wk, val);

	if (python_module_present(wk, _ctx->pythonpath, mod)) {
		return ir_cont;
	}

	if (_ctx->required) {
		interp_error(wk, _ctx->node, "python: required module '%s' not found", mod);
	}

	return ir_err;
}

static bool
func_module_python_find_installation(struct workspace *wk,
	obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_required,
		kw_disabler,
		kw_modules,
		kw_pure,
	};
	struct args_kw akw[] = {
		[kw_required] = { "required", obj_bool },
		[kw_disabler] = { "disabler", obj_bool },
		[kw_modules] = { "modules", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_pure] = { "pure", obj_bool },
		0
	};
	if (!interp_args(wk, args_node, NULL, ao, akw)) {
		return false;
	}

	bool required = !akw[kw_required].set || get_obj_bool(wk, akw[kw_required].val);
	bool disabler = akw[kw_disabler].set && get_obj_bool(wk, akw[kw_disabler].val);
	bool is_pure = true;

	if (akw[kw_pure].set) {
		is_pure = get_obj_bool(wk, akw[kw_pure].val);
	}

	const char *cmd = "python3";
	if (ao[0].set) {
		cmd = get_cstr(wk, ao[0].val);
	}

	SBUF(cmd_path);
	bool found = fs_find_cmd(wk, &cmd_path, cmd);
	if (required && !found) {
		interp_error(wk, args_node, "%s not found", cmd);
		return false;
	}

	if (!found && disabler) {
		*res = disabler_id;
		return true;
	}

	if (akw[kw_modules].set && found) {
		bool all_present = obj_array_foreach(wk,
			akw[kw_modules].val,
			&(struct iter_mod_ctx){
			.pythonpath = cmd_path.buf,
			.node = akw[kw_modules].node,
			.required = required,
		},
			iterate_required_module_list
			);

		if (!all_present) {
			if (required) {
				return false;
			}
			if (disabler) {
				*res = disabler_id;
				return true;
			}
			/* Return a not-found object. */
			found = false;
		}
	}

	make_obj(wk, res, obj_python_installation);
	struct obj_python_installation *python = get_obj_python_installation(wk, *res);

	make_obj(wk, &python->info.pure, obj_bool);
	set_obj_bool(wk, python->info.pure, is_pure);

	make_obj(wk, &python->prog, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, python->prog);
	ep->found = found;
	make_obj(wk, &ep->cmd_array, obj_array);
	obj_array_push(wk, ep->cmd_array, sbuf_into_str(wk, &cmd_path));

	if (!introspect_python_interpreter(wk, cmd_path.buf, python)) {
		interp_error(wk, args_node, "failed to introspect python");
		return false;
	}

	return true;
}

static bool
func_python_installation_language_version(struct workspace *wk, obj rcvr,
	uint32_t args_node, obj *res)
{
	if (!interp_args(wk, args_node, NULL, NULL, NULL)) {
		return false;
	}

	*res = get_obj_python_installation(wk, rcvr)->info.language_version;
	return true;
}

static bool
func_module_python3_find_python(struct workspace *wk, obj rcvr, uint32_t args_node, obj *res)
{
	struct args_norm ao[] = { { obj_string }, ARG_TYPE_NULL };
	if (!interp_args(wk, args_node, NULL, ao, NULL)) {
		return false;
	}

	const char *cmd = "python3";
	if (ao[0].set) {
		cmd = get_cstr(wk, ao[0].val);
	}

	SBUF(cmd_path);
	if (!fs_find_cmd(wk, &cmd_path, cmd)) {
		interp_error(wk, args_node, "python3 not found");
		return false;
	}

	make_obj(wk, res, obj_external_program);
	struct obj_external_program *ep = get_obj_external_program(wk, *res);
	ep->found = true;
	make_obj(wk, &ep->cmd_array, obj_array);
	obj_array_push(wk, ep->cmd_array, sbuf_into_str(wk, &cmd_path));

	return true;
}

static obj
python_rcvr_transform(struct workspace *wk, obj rcvr)
{
	return get_obj_python_installation(wk, rcvr)->prog;
}

void
python_build_impl_tbl(void)
{
	uint32_t i;
	for (i = 0; impl_tbl_external_program[i].name; ++i) {
		struct func_impl_name tmp = impl_tbl_external_program[i];
		tmp.rcvr_transform = python_rcvr_transform;
		impl_tbl_python_installation[i] = tmp;
	}
}

const struct func_impl_name impl_tbl_module_python[] = {
	{ "find_installation", func_module_python_find_installation, tc_python_installation },
	{ NULL, NULL },
};

const struct func_impl_name impl_tbl_module_python3[] = {
	{ "find_python", func_module_python3_find_python, tc_external_program },
	{ NULL, NULL },
};

struct func_impl_name impl_tbl_python_installation[] = {
	[ARRAY_LEN(impl_tbl_external_program) - 1] =
	{ "language_version", func_python_installation_language_version, tc_string },
	{ NULL, NULL },
};

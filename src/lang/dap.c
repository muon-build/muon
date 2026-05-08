/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "error.h"
#include "lang/dap.h"
#include "lang/object.h"
#include "lang/object_iterators.h"
#include "lang/server.h"
#include "lang/string.h"
#include "lang/vm.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/path.h"
#include "tracy.h"

enum dap_srv_state {
	dap_srv_state_uninitialized,
	dap_srv_state_initializing,
	dap_srv_state_initialized,
	dap_srv_state_running,
	dap_srv_state_stopped,
};

enum dap_srv_variable_group {
	dap_srv_variable_group_none,
	dap_srv_variable_group_globals,
	dap_srv_variable_group_locals,
	dap_srv_variable_group_upvalues,
	dap_srv_variable_group_container,
	dap_srv_variable_group_struct,
};

struct dap_srv {
	struct server srv;
	struct arena a, a_scratch;
	enum dap_srv_state state;
	uint32_t seq;
	bool pause_requested;
};

struct dap_request {
	obj seq, command, arguments;
};

static obj
dap_protocol_message(struct dap_srv *srv, struct workspace *wk, const char *type)
{
	++srv->seq;
	obj o = make_obj(wk, obj_dict);
	obj_dict_set(wk, o, make_str(wk, "seq"), make_number(wk, srv->seq));
	obj_dict_set(wk, o, make_str(wk, "type"), make_str(wk, type));
	return o;
}

static void
dap_respond(struct dap_srv *srv, struct workspace *wk, struct dap_request *req, obj body)
{
	obj rsp = dap_protocol_message(srv, wk, "response");
	obj_dict_set(wk, rsp, make_str(wk, "request_seq"), req->seq);
	obj_dict_set(wk, rsp, make_str(wk, "success"), obj_bool_true);
	obj_dict_set(wk, rsp, make_str(wk, "command"), req->command);
	// obj_dict_set(wk, rsp, make_str(wk, "message"), err_msg);
	if (body) {
		obj_dict_set(wk, rsp, make_str(wk, "body"), body);
	}

	srv_write(&srv->srv, wk, rsp);
}

static void
dap_event(struct dap_srv *srv, struct workspace *wk, const char *event, obj body)
{
	obj ev = dap_protocol_message(srv, wk, "event");
	obj_dict_set(wk, ev, make_str(wk, "event"), make_str(wk, event));
	if (body) {
		obj_dict_set(wk, ev, make_str(wk, "body"), body);
	}

	srv_write(&srv->srv, wk, ev);
}

static obj
dap_stack_frame(struct dap_srv *srv, struct workspace *wk, struct call_frame *frame, uint32_t ip, uint32_t idx)
{
	struct vm_inst_location loc;
	vm_inst_location(srv->srv.wk, ip, &loc);

	obj f = make_obj(wk, obj_dict);
	obj file = make_str(wk, loc.file);

	obj name;
	if (frame->closure->func->name) {
		name = make_str(wk, frame->closure->func->name);
	} else if (frame->eval_name) {
		TSTR(buf);
		path_relative_to(wk, &buf, srv->srv.wk->source_root, get_str(srv->srv.wk, frame->eval_name)->s);

		name = tstr_into_str(wk, &buf);
	} else {
		name = make_str(wk, "anonymous function");
	}

	obj_dict_set(wk, f, make_str(wk, "id"), make_number(wk, idx));
	obj_dict_set(wk, f, make_str(wk, "name"), name);
	if (!loc.embedded) {
		obj source = make_obj(wk, obj_dict);
		obj_dict_set(wk, source, make_str(wk, "path"), file);
		obj_dict_set(wk, f, make_str(wk, "source"), source);
		obj_dict_set(wk, f, make_str(wk, "line"), make_number(wk, loc.line));
		obj_dict_set(wk, f, make_str(wk, "column"), make_number(wk, loc.col));
	}

	return f;
}

union dap_variable_ref {
	int64_t num;
	struct {
		uint32_t group;
		uint32_t data;
	} dat;
};

static obj
dap_scope_group(struct workspace *wk, enum dap_srv_variable_group group, uint32_t frame_idx)
{
	const char *name = 0;
	switch (group) {
	case dap_srv_variable_group_container:
	case dap_srv_variable_group_struct:
	case dap_srv_variable_group_none: UNREACHABLE_RETURN;
	case dap_srv_variable_group_globals: name = "globals"; break;
	case dap_srv_variable_group_locals: name = "locals"; break;
	case dap_srv_variable_group_upvalues: name = "upvalues"; break;
	}

	union dap_variable_ref ref = {
		.dat = { group, frame_idx },
	};

	obj scope = make_obj(wk, obj_dict);
	obj_dict_set(wk, scope, make_str(wk, "name"), make_str(wk, name));
	obj_dict_set(wk, scope, make_str(wk, "variablesReference"), make_number(wk, ref.num));
	return scope;
}

static obj
dap_variable_simple(struct workspace *wk, obj name, obj val, enum obj_type type)
{
	obj variable = make_obj(wk, obj_dict);
	obj_dict_set(wk, variable, make_str(wk, "name"), name);
	obj_dict_set(wk, variable, make_str(wk, "value"), val);
	obj_dict_set(wk, variable, make_str(wk, "type"), make_str(wk, obj_type_to_s(type)));
	return variable;
}

static obj
dap_variable_named(struct dap_srv *srv, struct workspace *wk, obj name, obj val)
{
	obj variable = make_obj(wk, obj_dict);
	obj_dict_set(wk, variable, make_str(wk, "name"), name);

	TSTR(buf);
	obj_to_s(srv->srv.wk, val, &buf);
	obj_dict_set(wk, variable, make_str(wk, "value"), tstr_into_str(wk, &buf));

	enum obj_type t = get_obj_type(srv->srv.wk, val);
	obj_dict_set(wk, variable, make_str(wk, "type"), make_str(wk, obj_type_to_s(t)));

	enum dap_srv_variable_group group = 0;

	switch (t) {
	case obj_null:
	case obj_disabler:
	case obj_bool:
	case obj_number:
	case obj_string: break;
	case obj_array:
	case obj_dict:
		group = dap_srv_variable_group_container;
		break;
	default:
		if (vm_reflected_obj_fields(srv->srv.wk, t)) {
			group = dap_srv_variable_group_struct;
		}
		break;
	}

	if (group) {
		union dap_variable_ref ref = {
			.dat = { group, val },
		};

		obj_dict_set(wk, variable, make_str(wk, "variablesReference"), make_number(wk, ref.num));
	}

	return variable;
}

static obj
dap_variable(struct dap_srv *srv, struct workspace *wk, obj name, obj val)
{
	return dap_variable_named(srv, wk, make_strs(wk, get_str(srv->srv.wk, name)), val);
}

static void
dap_handle(struct dap_srv *srv, struct workspace *wk, obj msg)
{
	struct dap_request req = {
		.seq = obj_dict_index_as_obj(wk, msg, "seq"),
		.command = obj_dict_index_as_obj(wk, msg, "command"),
		.arguments = obj_dict_index_as_obj(wk, msg, "arguments"),
	};
	const struct str *command = get_str(wk, req.command);
	if (str_eql(command, &STR("initialize"))) {
		srv->state = dap_srv_state_initializing;
		obj body = make_obj(wk, obj_dict);
		// dcmake doesn't care about anything other than
		// exceptionBreakpointFilters?
		dap_respond(srv, wk, &req, body);
	} else if (str_eql(command, &STR("configurationDone"))) {
		srv->state = dap_srv_state_running;
		dap_respond(srv, wk, &req, 0);
	} else if (str_eql(command, &STR("pause"))) {
		srv->pause_requested = true;
		srv->srv.wk->vm.dbg_state.break_on_entry = true;
		dap_respond(srv, wk, &req, 0);
	} else if (str_eql(command, &STR("setBreakpoints"))) {

		obj source = obj_dict_index_as_obj(wk, req.arguments, "source");
		obj path = make_strs(srv->srv.wk, obj_dict_index_as_str(wk, source, "path"));

		vm_dbg_clear_breakpoints_for_src(srv->srv.wk, path);

		obj breakpoints = obj_dict_index_as_obj(wk, req.arguments, "breakpoints");
		obj breakpoint;
		obj_array_for(wk, breakpoints, breakpoint) {
			int64_t line = obj_dict_index_as_number(wk, breakpoint, "line");
			vm_dbg_push_breakpoint(srv->srv.wk, path, line, 1);
		}
	} else if (str_eql(command, &STR("stackTrace"))) {
		obj frames = make_obj(wk, obj_array);
		{
			{ // push current location as a frame
				struct call_frame *frame
					= arr_get(&srv->srv.wk->vm.call_stack, srv->srv.wk->vm.call_stack.len - 1);
				obj f = dap_stack_frame(srv,
					wk,
					frame,
					srv->srv.wk->vm.dbg_state.cur_bp->ip,
					srv->srv.wk->vm.call_stack.len);
				obj_array_push(wk, frames, f);
			}

			for (int32_t i = srv->srv.wk->vm.call_stack.len - 1; i >= 0; --i) {
				struct call_frame *frame = arr_get(&srv->srv.wk->vm.call_stack, i);

				if (!frame->return_ip || frame->closure->func->wrapper) {
					continue;
				}

				struct call_frame *prev_frame = i > 0 ? arr_get(&srv->srv.wk->vm.call_stack, i - 1) :
									frame;

				obj f = dap_stack_frame(srv, wk, prev_frame, frame->return_ip - OP_WIDTH(op_call), i);
				obj_array_push(wk, frames, f);
			}
		}

		obj body = make_obj(wk, obj_dict);
		obj_dict_set(wk, body, make_str(wk, "stackFrames"), frames);

		dap_respond(srv, wk, &req, body);
	} else if (str_eql(command, &STR("scopes"))) {
		int64_t frame_id = obj_dict_index_as_number(wk, req.arguments, "frameId");
		uint32_t frame_idx = frame_id == srv->srv.wk->vm.call_stack.len ? srv->srv.wk->vm.call_stack.len - 1 : frame_id;

		struct call_frame *frame = arr_get(&srv->srv.wk->vm.call_stack, frame_idx);

		obj scopes = make_obj(wk, obj_array);

		if (frame->closure->func->automatically_defined && !frame->closure->func->wrapper)  {
			// eval frame, globals only
			obj_array_push(wk, scopes, dap_scope_group(wk, dap_srv_variable_group_globals, frame_idx));
		} else {
			// script frame, locals and upvalues only
			obj_array_push(wk, scopes, dap_scope_group(wk, dap_srv_variable_group_locals, frame_idx));
			obj_array_push(wk, scopes, dap_scope_group(wk, dap_srv_variable_group_upvalues, frame_idx));
		}

		obj body = make_obj(wk, obj_dict);
		obj_dict_set(wk, body, make_str(wk, "scopes"), scopes);

		dap_respond(srv, wk, &req, body);
	} else if (str_eql(command, &STR("variables"))) {
		union dap_variable_ref ref = {
			.num = obj_dict_index_as_number(wk, req.arguments, "variablesReference"),
		};

		obj variables = make_obj(wk, obj_array);

		switch (ref.dat.group) {
		case dap_srv_variable_group_globals: {
			obj k, v;
			obj_dict_for(srv->srv.wk, srv->srv.wk->vm.global_scope, k, v) {
				obj_array_push(wk, variables, dap_variable(srv, wk, k, v));
			}
			break;
		}
		case dap_srv_variable_group_locals: {
			struct call_frame *frame = arr_get(&srv->srv.wk->vm.call_stack, ref.dat.data);

			for (uint32_t i = 0; i < frame->closure->func->nlocals; ++i) {
				obj slot_idx = i + frame->stack_base;
				const struct obj_stack_entry *slot
					= bucket_arr_get(&srv->srv.wk->vm.stack.ba, slot_idx);
				obj var = dap_variable(srv, wk, frame->closure->func->locals_debug[i], slot->o);
				obj_array_push(wk, variables, var);
			}
			break;
		}
		case dap_srv_variable_group_upvalues: {
			struct call_frame *frame = arr_get(&srv->srv.wk->vm.call_stack, ref.dat.data);
			for (uint32_t i = 0; i < frame->closure->func->nupvalues; ++i) {
				obj var = dap_variable(srv,
					wk,
					frame->closure->upvalues[i]->debug_id,
					*frame->closure->upvalues[i]->location);
				obj_array_push(wk, variables, var);
			}
			break;
		}
		case dap_srv_variable_group_container: {
			obj container = ref.dat.data;
			switch (get_obj_type(srv->srv.wk, container)) {
			case obj_array: {
				obj v;
				obj_array_for_(srv->srv.wk, container, v, iter) {
					obj var = dap_variable_named(srv, wk, make_strf(wk, "[%d]", iter.i), v);
					obj_array_push(wk, variables, var);
				}
				break;
			}
			case obj_dict: {
				obj k, v;
				obj_dict_for(srv->srv.wk, container, k, v) {
					obj var = dap_variable(srv, wk, k, v);
					obj_array_push(wk, variables, var);
				}
				break;
			}
			default: break;
			}
			break;
		}
		case dap_srv_variable_group_struct: {
			obj val = ref.dat.data;
			enum obj_type t = get_obj_type(srv->srv.wk, val);
			obj fields = vm_reflected_obj_fields(srv->srv.wk, t);
			char *src = get_obj_internal(srv->srv.wk, val, t);

			obj fi;
			const struct vm_reflected_field *f;
			obj_array_for(srv->srv.wk, fields, fi) {
				f = vm_reflected_obj_field(srv->srv.wk, fi);
				void *src_field = src + f->off;
				obj name = f->name ? make_str(wk, f->name) : make_str(wk, "[0]");
				if (strcmp(f->type, "obj") == 0) {
					obj var = dap_variable_named(srv, wk, name, *(obj*)src_field);
					obj_array_push(wk, variables, var);
				} else if (strcmp(f->type, "int32_t") == 0) {
					obj var = dap_variable_simple(wk, name, make_strf(wk, "%d", *(int32_t *)src_field), obj_number);
					obj_array_push(wk, variables, var);
				} else if (strcmp(f->type, "uint32_t") == 0) {
					obj var = dap_variable_simple(wk, name, make_strf(wk, "%d", *(uint32_t *)src_field), obj_number);
					obj_array_push(wk, variables, var);
				} else if (strcmp(f->type, "bool") == 0) {
					obj var = dap_variable_simple(wk,
						name,
						make_str(wk, *(bool *)src_field ? "true" : "false"),
						obj_bool);
					obj_array_push(wk, variables, var);
				} else {
					// TODO: show the numeric value for enums?
					obj var = dap_variable_simple(wk, name, make_str(wk, f->type), obj_string);
					obj_array_push(wk, variables, var);
				}
			}
		}
		}

		obj body = make_obj(wk, obj_dict);
		obj_dict_set(wk, body, make_str(wk, "variables"), variables);

		dap_respond(srv, wk, &req, body);
	} else if (str_eql(command, &STR("next"))) {
		vm_dbg_prepare_step(srv->srv.wk, vm_dbg_step_flag_line);
		srv->state = dap_srv_state_running;
		dap_respond(srv, wk, &req, 0);
	} else if (str_eql(command, &STR("stepIn"))) {
		vm_dbg_prepare_step(srv->srv.wk, vm_dbg_step_flag_line | vm_dbg_step_flag_in);
		srv->state = dap_srv_state_running;
		dap_respond(srv, wk, &req, 0);
	} else if (str_eql(command, &STR("stepOut"))) {
		vm_dbg_prepare_step(srv->srv.wk, vm_dbg_step_flag_line | vm_dbg_step_flag_out);
		srv->state = dap_srv_state_running;
		dap_respond(srv, wk, &req, 0);
	} else if (str_eql(command, &STR("continue"))) {
		srv->state = dap_srv_state_running;
		dap_respond(srv, wk, &req, 0);
	}
}

void
dap_destroy(struct workspace *srv_wk)
{
	struct dap_srv *srv = srv_wk->vm.dbg_state.dap;
	srv_wk->vm.dbg_state.dap = 0;
	srv_destroy(&srv->srv);
	ar_destroy(&srv->a);
	ar_destroy(&srv->a_scratch);
}

static struct workspace *
dap_server_begin(struct workspace *srv_wk)
{
	struct dap_srv *srv = srv_wk->vm.dbg_state.dap;

	workspace_scratch_begin(srv_wk);

	struct workspace *wk = ar_make(&srv->a, struct workspace);
	*wk = (struct workspace ) { .a = &srv->a, .a_scratch = &srv->a_scratch };
	workspace_init_bare(wk, &srv->a, &srv->a_scratch);

	return wk;
}

static void
dap_server_end(struct workspace *srv_wk)
{
	struct dap_srv *srv = srv_wk->vm.dbg_state.dap;

	log_flush();

	workspace_scratch_end(srv_wk);

	ar_clear(&srv->a);
	ar_clear(&srv->a_scratch);
}

bool
dap_server_update(struct workspace *srv_wk)
{
	struct dap_srv *srv = srv_wk->vm.dbg_state.dap;

	// log_set_indent(0);

	struct workspace *wk = dap_server_begin(srv_wk);

	if (srv->state == dap_srv_state_initializing) {
		dap_event(srv, wk, "initialized", 0);
		srv->state = dap_srv_state_initialized;
	}

	obj msg;
	switch (srv_read(&srv->srv, wk, &msg)) {
	case srv_read_result_err: {
		dap_destroy(srv_wk);
		return false;
	}
	case srv_read_result_eof: {
		dap_destroy(srv_wk);
		return true;
	}
	case srv_read_result_ok: break;
	}

	dap_handle(srv, wk, msg);

	dap_server_end(srv_wk);

	return true;
}

static void
dap_break_cb(struct workspace *srv_wk)
{
	struct dap_srv *srv = srv_wk->vm.dbg_state.dap;

	struct workspace *wk = dap_server_begin(srv_wk);

	obj body = make_obj(wk, obj_dict);
	if (srv->pause_requested) {
		obj_dict_set(wk, body, make_str(wk, "reason"), make_str(wk, "pause"));
		obj_dict_set(wk, body, make_str(wk, "threadId"), make_number(wk, 1));
		srv->pause_requested = false;
	} else {
		obj_dict_set(wk, body, make_str(wk, "reason"), make_str(wk, "breakpoint"));
	}
	dap_event(srv, wk, "stopped", body);
	srv->state = dap_srv_state_stopped;

	dap_server_end(srv_wk);

	while (srv->state != dap_srv_state_running) {
		if (!dap_server_update(srv_wk)) {
			// Should we just die here?
			return;
		}
	}
}

bool
dap_init_pipe(struct workspace *srv_wk, const char *pipe_path)
{
	struct dap_srv *srv = ar_make(srv_wk->a, struct dap_srv);
	srv_wk->vm.dbg_state.dap = srv;
	srv_wk->vm.dbg_state.break_cb = dap_break_cb;

	if (!srv_init_pipe(srv_wk, &srv->srv, pipe_path)) {
		return false;
	}

	arena_init(&srv->a,);
	arena_init(&srv->a_scratch,);

	while (srv->state != dap_srv_state_running) {
		if (!dap_server_update(srv_wk)) {
			return false;
		}
	}

	return true;
}

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "lang/dap.h"
#include "lang/object_iterators.h"
#include "lang/server.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/assert.h"
#include "tracy.h"

enum dap_srv_state {
	dap_srv_state_uninitialized,
	dap_srv_state_initializing,
	dap_srv_state_initialized,
	dap_srv_state_running,
	dap_srv_state_stopped,
};

struct dap_srv {
	struct server srv;
	struct arena a, a_scratch;
	enum dap_srv_state state;
	uint32_t seq;
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
dap_stack_frame(struct dap_srv *srv, struct workspace *wk, uint32_t ip)
{
	struct vm_inst_location loc;
	vm_inst_location(srv->srv.wk, ip, &loc);

	obj frame = make_obj(wk, obj_dict);
	obj file = make_str(wk, loc.file);

	obj_dict_set(wk, frame, make_str(wk, "id"), make_number(wk, 0));
	obj_dict_set(wk, frame, make_str(wk, "name"), file);
	if (!loc.embedded) {
		obj source = make_obj(wk, obj_dict);
		obj_dict_set(wk, source, make_str(wk, "path"), file);
		obj_dict_set(wk, frame, make_str(wk, "source"), source);
		obj_dict_set(wk, frame, make_str(wk, "line"), make_number(wk, loc.line));
		obj_dict_set(wk, frame, make_str(wk, "column"), make_number(wk, loc.col));
	}

	return frame;
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
	} else if (str_eql(command, &STR("setBreakpoints"))) {
		obj source = obj_dict_index_as_obj(wk, req.arguments, "source");
		obj path = make_strs(srv->srv.wk, obj_dict_index_as_str(wk, source, "path"));
		obj breakpoints = obj_dict_index_as_obj(wk, req.arguments, "breakpoints");
		obj breakpoint;
		obj_array_for(wk, breakpoints, breakpoint) {
			int64_t line = obj_dict_index_as_number(wk, breakpoint, "line");
			vm_dbg_push_breakpoint(srv->srv.wk, path, line, 1);
		}
	} else if (str_eql(command, &STR("stackTrace"))) {
		obj frames = make_obj(wk, obj_array);
		{
			obj_array_push(wk, frames, dap_stack_frame(srv, wk, srv->srv.wk->vm.dbg_state.cur_bp->ip));

// 			for (int32_t i = srv->srv.wk->vm.call_stack.len - 1; i >= 0; --i) {
// 				struct call_frame *frame = arr_get(&srv->srv.wk->vm.call_stack, i);
// 				if (!frame->return_ip) {
// 					continue;
// 				}
// 				obj_array_push(
// 					wk, frames, dap_stack_frame(srv, wk, frame->return_ip - OP_WIDTH(op_call)));
// 			}
		}

		obj body = make_obj(wk, obj_dict);
		obj_dict_set(wk, body, make_str(wk, "stackFrames"), frames);

		dap_respond(srv, wk, &req, body);
	} else if (str_eql(command, &STR("scopes"))) {
		int64_t frame_id = obj_dict_index_as_number(wk, req.arguments, "frameId");

		obj scopes = make_obj(wk, obj_array);
		obj scope = make_obj(wk, obj_dict);
		obj_dict_set(wk, scope, make_str(wk, "name"), make_str(wk, "globals"));
		obj_dict_set(wk, scope, make_str(wk, "variablesReference"), make_number(wk, 1));
		obj_array_push(wk, scopes, scope);

		obj body = make_obj(wk, obj_dict);
		obj_dict_set(wk, body, make_str(wk, "scopes"), scopes);

		dap_respond(srv, wk, &req, body);
	} else if (str_eql(command, &STR("variables"))) {
		int64_t ref = obj_dict_index_as_number(wk, req.arguments, "variablesReference");

		obj variables = make_obj(wk, obj_array);

		if (ref == 1) {
			obj k, v;
			obj_dict_for(srv->srv.wk, srv->srv.wk->vm.global_scope, k, v) {
				obj variable = make_obj(wk, obj_dict);
				obj_dict_set(wk, variable, make_str(wk, "name"), make_strs(wk, get_str(srv->srv.wk, k)));

				TSTR(buf);
				obj_to_s(srv->srv.wk, v, &buf);
				obj_dict_set(wk, variable, make_str(wk, "value"), tstr_into_str(wk, &buf));
				obj_array_push(wk, variables, variable);
			}
		}

		obj body = make_obj(wk, obj_dict);
		obj_dict_set(wk, body, make_str(wk, "variables"), variables);

		dap_respond(srv, wk, &req, body);
	} else if (str_eql(command, &STR("next"))) {
		vm_dbg_prepare_step(srv->srv.wk, vm_dbg_step_flag_line);
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

	obj_lprintf(wk, log_debug, "<<< %#o\n", msg);

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
	obj_dict_set(wk, body, make_str(wk, "reason"), make_str(wk, "breakpoint"));
	dap_event(srv, wk, "stopped", body);
	srv->state = dap_srv_state_stopped;

	dap_server_end(srv_wk);

	while (srv->state != dap_srv_state_running) {
		if (!dap_server_update(srv_wk)) {
			// Hmm?
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

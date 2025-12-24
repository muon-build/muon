/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "error.h"
#include "formats/ini_cfg.h"
#include "formats/json.h"
#include "functions/modules.h"
#include "lang/analyze.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "memmem.h"
#include "platform/assert.h"
#include "platform/os.h"
#include "platform/path.h"
#include "tracy.h"
#include "version.h"

enum az_srv_req_type {
	az_srv_req_type_completion,
	az_srv_req_type_hover,
	az_srv_req_type_definition,
};

struct az_srv {
	struct {
		struct tstr *in_buf;
		int in;
		FILE *out;
	} transport;

	bool verbose;
	bool should_analyze;

	struct {
		obj id;
		obj result;
		enum az_srv_req_type type;
	} req;

	struct workspace *wk;
	obj diagnostics_to_clear;
	struct az_opts opts;
};

enum LspTextDocumentSyncKind {
	LspTextDocumentSyncKindNone = 0,
	LspTextDocumentSyncKindFull = 1,
	LspTextDocumentSyncKindIncremental = 2,
};
enum LspDiagnosticSeverity {
	LspDiagnosticSeverityError = 1,
	LspDiagnosticSeverityWarning = 2,
	LspDiagnosticSeverityInformation = 3,
	LspDiagnosticSeverityHint = 4,
};
static const enum LspDiagnosticSeverity lsp_severity_map[] = {
	[log_error] = LspDiagnosticSeverityError,
	[log_warn] = LspDiagnosticSeverityWarning,
	[log_info] = LspDiagnosticSeverityInformation,
	[log_debug] = LspDiagnosticSeverityHint,
};
enum LspCompletionItemKind {
	LspCompletionItemKindText = 1,
	LspCompletionItemKindMethod = 2,
	LspCompletionItemKindFunction = 3,
	LspCompletionItemKindConstructor = 4,
	LspCompletionItemKindField = 5,
	LspCompletionItemKindVariable = 6,
	LspCompletionItemKindClass = 7,
	LspCompletionItemKindInterface = 8,
	LspCompletionItemKindModule = 9,
	LspCompletionItemKindProperty = 10,
	LspCompletionItemKindUnit = 11,
	LspCompletionItemKindValue = 12,
	LspCompletionItemKindEnum = 13,
	LspCompletionItemKindKeyword = 14,
	LspCompletionItemKindSnippet = 15,
	LspCompletionItemKindColor = 16,
	LspCompletionItemKindFile = 17,
	LspCompletionItemKindReference = 18,
	LspCompletionItemKindFolder = 19,
	LspCompletionItemKindEnumMember = 20,
	LspCompletionItemKindConstant = 21,
	LspCompletionItemKindStruct = 22,
	LspCompletionItemKindEvent = 23,
	LspCompletionItemKindOperator = 24,
	LspCompletionItemKindTypeParameter = 25,
};

enum az_srv_read_result {
	az_srv_read_result_err,
	az_srv_read_result_ok,
	az_srv_read_result_eof,
};

static enum az_srv_read_result
az_srv_read_bytes(struct az_srv *srv)
{
	struct tstr *buf = srv->transport.in_buf;

	if (buf->cap - buf->len < 16) {
		tstr_grow(srv->wk, buf, 1024);
	}

	const uint32_t space_available = buf->cap - buf->len;
	uint32_t bytes_available = space_available;
	if (!fs_wait_for_input(srv->transport.in, &bytes_available)) {
		return az_srv_read_result_err;
	}

	if (bytes_available > space_available) {
		bytes_available = space_available;
	}

	int32_t n = fs_read(srv->transport.in, buf->buf + buf->len, bytes_available);
	if (n <= 0) {
		return az_srv_read_result_eof;
	}

	buf->len += n;

	return az_srv_read_result_ok;
}

static void
az_srv_buf_shift(struct az_srv *srv, uint32_t amnt)
{
	struct tstr *buf = srv->transport.in_buf;

	char *start = buf->buf + amnt;
	buf->len = buf->len - (start - buf->buf);
	memmove(buf->buf, start, buf->len);
}

static enum az_srv_read_result
az_srv_read(struct az_srv *srv, struct workspace *wk, obj *msg)
{
	TracyCZoneAutoS;
	int64_t content_length = 0;
	struct tstr *buf = srv->transport.in_buf;

	{
		char *end;
		while (!(end = memmem(buf->buf, buf->len, "\r\n\r\n", 4))) {
			switch(az_srv_read_bytes(srv)) {
			case az_srv_read_result_err:
				LOG_E("error when reading message header");
				return az_srv_read_result_err;
			case az_srv_read_result_ok:
				break;
			case az_srv_read_result_eof:
				if (buf->len) {
					LOG_E("eof when reading message header");
					return az_srv_read_result_err;
				}
				return az_srv_read_result_eof;
			}
		}

		*(end + 2) = 0;

		const char *hdr = buf->buf;
		char *hdr_end;
		while ((hdr_end = strstr(hdr, "\r\n"))) {
			*hdr_end = 0;
			char *val;
			if ((val = strchr(buf->buf, ':'))) {
				*val = 0;
				val += 2;

				if (strcmp(hdr, "Content-Length") == 0) {
					if (!str_to_i(&STRL(val), &content_length, false)) {
						LOG_E("Invalid value for Content-Length");
					}
				} else if (strcmp(hdr, "Content-Type") == 0) {
					// Ignore
				} else {
					LOG_E("Unknown header: %s", hdr);
				}
			} else {
				LOG_E("Header missing ':': %s", hdr);
			}

			if (hdr_end == end) {
				break;
			}
		}

		if (!content_length) {
			LOG_E("Missing Content-Length header.");
			return false;
		}

		az_srv_buf_shift(srv, (hdr_end - buf->buf) + 4);
	}

	{
		while (buf->len < content_length) {
			switch (az_srv_read_bytes(srv)) {
			case az_srv_read_result_err:
				LOG_E("error when reading message body");
				return az_srv_read_result_err;
			case az_srv_read_result_eof:
				LOG_E("eof when reading message body");
				return az_srv_read_result_err;
			case az_srv_read_result_ok:
				break;
			}
		}

		char end = buf->buf[content_length];
		buf->buf[content_length] = 0;
		const struct str json_msg = { .s = buf->buf, .len = content_length };
		if (!muon_json_to_obj(wk, &json_msg, msg)) {
			obj_lprintf(wk, log_error, "failed to parse json: %o", *msg);
			return false;
		} else if (get_obj_type(wk, *msg) != obj_dict) {
			obj_lprintf(wk, log_error, "message was not a dict, got %s", obj_type_to_s(get_obj_type(wk, *msg)));
			return false;
		}
		buf->buf[content_length] = end;

		az_srv_buf_shift(srv, content_length);
	}

	TracyCZoneAutoE;
	return true;
}

static void
az_srv_write(struct az_srv *srv, struct workspace *wk, obj msg)
{
	obj_lprintf(wk, log_debug, ">>> %#o\n", msg);
	TSTR(buf);
	if (!obj_to_json(wk, msg, &buf)) {
		UNREACHABLE;
	}

	fprintf(srv->transport.out, "Content-Length: %d\r\n\r\n%s", buf.len, buf.buf);
	fflush(srv->transport.out);
}

static obj
az_srv_jsonrpc_msg(struct workspace *wk)
{
	obj o = make_obj(wk, obj_dict);
	obj_dict_set(wk, o, make_str(wk, "jsonrpc"), make_str(wk, "2.0"));
	return o;
}

static void
az_srv_respond(struct az_srv *srv, struct workspace *wk, obj id, obj result)
{
	obj rsp = az_srv_jsonrpc_msg(wk);

	obj_dict_set(wk, rsp, make_str(wk, "id"), id);
	obj_dict_set(wk, rsp, make_str(wk, "result"), result);

	az_srv_write(srv, wk, rsp);
}

static void
az_srv_request(struct az_srv *srv, struct workspace *wk, const char *method, obj params)
{
	obj req = az_srv_jsonrpc_msg(wk);
	obj_dict_set(wk, req, make_str(wk, "method"), make_str(wk, method));
	obj_dict_set(wk, req, make_str(wk, "params"), params);

	az_srv_write(srv, wk, req);
}

static void az_srv_log(struct az_srv *srv, struct workspace *wk, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);

static void
az_srv_log(struct az_srv *srv, struct workspace *wk, const char *fmt, ...)
{
	if (!srv->verbose)
	{
		return;
	}

	va_list ap;
	va_start(ap, fmt);
	TSTR(tstr);
	obj_vasprintf(wk, &tstr, fmt, ap);

	obj params = make_obj(wk, obj_dict);
	obj_dict_set(wk, params, make_str(wk, "message"), make_strf(wk, "muon: %s\n", tstr.buf));
	az_srv_request(srv, wk, "$/logTrace", params);
}

static obj
az_srv_position(struct workspace *wk, uint32_t line, uint32_t col)
{
	obj d = make_obj(wk, obj_dict);
	obj_dict_set(wk, d, make_str(wk, "line"), make_number(wk, line - 1));
	obj_dict_set(wk, d, make_str(wk, "character"), make_number(wk, col ? col - 1 : 0));
	return d;
}

static obj
az_srv_range(struct workspace *wk, const struct source *src, struct source_location loc)
{
	struct source src_reopened = { 0 };
	reopen_source(wk->a_scratch, src, &src_reopened);

	struct detailed_source_location dloc;
	get_detailed_source_location(&src_reopened, loc, &dloc, get_detailed_source_location_flag_multiline);

	obj range = make_obj(wk, obj_dict);
	obj_dict_set(wk, range, make_str(wk, "start"), az_srv_position(wk, dloc.line, dloc.col));
	obj_dict_set(wk,
		range,
		make_str(wk, "end"),
		az_srv_position(
			wk, dloc.end_line ? dloc.end_line : dloc.line, dloc.end_col ? dloc.end_col + 1 : dloc.end_col));

	return range;
}

static const char *
az_srv_uri_to_path(struct workspace *wk, const struct str *_uri_s)
{
	struct str uri_s = *_uri_s;
	{
		const struct str file_prefix = STR("file://");
		if (!str_startswith(&uri_s, &file_prefix)) {
			return 0;
		}

		uri_s.s += file_prefix.len;
		uri_s.len -= file_prefix.len;
	}

	TSTR(res);
	if (!str_percent_decode(wk, &uri_s, &res)) {
		return 0;
	}

	// Trim leading / from path on windows
	if (host_machine.is_windows && res.len >= 4 && path_begins_with_win32_drive(res.buf + 1)) {
		++res.buf;
		--res.len;
		res.buf[res.len] = 0;
	}


	return get_str(wk, tstr_into_str(wk, &res))->s;
}

static obj
az_srv_path_to_uri(struct workspace *wk, const struct str *path)
{
	TSTR(res);

	tstr_pushs(wk, &res, "file://");

	if (host_machine.is_windows && path_begins_with_win32_drive(path->s)) {
		tstr_push(wk, &res, '/');
	}

	str_percent_encode(wk, path, &res);

	return tstr_into_str(wk, &res);
}

static void
az_srv_diagnostics(struct az_srv *srv, struct workspace *wk, const struct source *src, obj list)
{
	if (src->type != source_type_file) {
		return;
	}

	obj params = make_obj(wk, obj_dict);
	obj uri = az_srv_path_to_uri(wk, &STRL(src->label));
	obj_dict_set(wk, params, make_str(wk, "uri"), uri);
	obj_dict_set(wk, params, make_str(wk, "diagnostics"), list ? list : make_obj(wk, obj_array));
	az_srv_request(srv, wk, "textDocument/publishDiagnostics", params);

	if (list) {
		obj_array_push(srv->wk, srv->diagnostics_to_clear, make_str(srv->wk, src->label));
	}
}

static void
az_srv_all_diagnostics(struct az_srv *srv, struct workspace *wk)
{
	{
		obj diagnostics_to_clear;
		obj_clone(srv->wk, wk, srv->diagnostics_to_clear, &diagnostics_to_clear);
		obj_array_clear(srv->wk, srv->diagnostics_to_clear);

		az_srv_log(srv, wk, "clearing diagnostics from %o", diagnostics_to_clear);

		obj uri;
		obj_array_for(wk, diagnostics_to_clear, uri) {
			az_srv_diagnostics(
				srv, wk, &(struct source){ .type = source_type_file, .label = get_cstr(wk, uri) }, 0);
		}
	}

	const struct arr *diagnostics = &wk->diagnostic_store->messages;

	uint32_t i;
	struct error_diagnostic_message *msg;
	const struct source *last_src = 0, *cur_src;
	struct source null_src = { 0 };

	obj d = make_obj(wk, obj_array);

	for (i = 0; i < diagnostics->len; ++i) {
		msg = arr_get(diagnostics, i);
		cur_src = msg->src_idx == UINT32_MAX ? &null_src : arr_get(&wk->vm.src, msg->src_idx);
		if (cur_src != last_src) {
			if (last_src) {
				az_srv_diagnostics(srv, wk, last_src, d);
				obj_array_clear(wk, d);
			}

			last_src = cur_src;
		}

		obj diagnostic = make_obj(wk, obj_dict);
		obj_dict_set(wk, diagnostic, make_str(wk, "range"), az_srv_range(wk, cur_src, msg->location));
		obj_dict_set(wk, diagnostic, make_str(wk, "severity"), make_number(wk, lsp_severity_map[msg->lvl]));
		obj_dict_set(wk, diagnostic, make_str(wk, "message"), make_str(wk, msg->msg));
		obj_array_push(wk, d, diagnostic);
	}

	if (last_src) {
		az_srv_diagnostics(srv, wk, last_src, d);
	}
}

static void
az_srv_set_src_override(struct az_srv *srv, struct workspace *wk, const struct str *uri_s, const struct str *content)
{
	const char *path;
	if (!(path = az_srv_uri_to_path(wk, uri_s))) {
		return;
	}

	srv->should_analyze = true;
	analyze_opts_push_override(srv->wk, &srv->opts, path, 0, content);
}

static void
az_srv_handle_push_breakpoint_from_msg(struct az_srv *srv, struct workspace *wk, obj msg)
{
	obj params = obj_dict_index_as_obj(wk, msg, "params");
	obj text_document = obj_dict_index_as_obj(wk, params, "textDocument");
	const struct str *uri = obj_dict_index_as_str(wk, text_document, "uri");
	obj position = obj_dict_index_as_obj(wk, params, "position");
	int64_t line = obj_dict_index_as_number(wk, position, "line");
	int64_t col = obj_dict_index_as_number(wk, position, "character");

	const char *path = az_srv_uri_to_path(wk, uri);
	if (path && line >= 0 && col >= 0) {
		vm_dbg_push_breakpoint(wk, make_str(wk, path), line + 1, col + 1);
		srv->should_analyze = true;
	} else {
		LOG_E("unable to push breakpoint for %s:%d:%d", uri->s, (int32_t)line, (int32_t)col);
	}
}

static void
az_srv_handle(struct az_srv *srv, struct workspace *wk, obj msg)
{
	TracyCZoneAutoS;
	const struct str *method = obj_dict_index_as_str(wk, msg, "method");

	if (str_eql(method, &STR("initialize"))) {
		obj params = obj_dict_index_as_obj(wk, msg, "params");
		obj root_uri = obj_dict_index_as_obj(wk, params, "rootUri");
		if (root_uri) {
			const char *root = az_srv_uri_to_path(wk, get_str(wk, root_uri));
			if (root) {
				path_chdir(wk, root);
			}
		}

		obj result = make_obj(wk, obj_dict);
		obj capabilities = make_obj(wk, obj_dict);
		obj_dict_set(wk,
			capabilities,
			make_str(wk, "textDocumentSync"),
			make_number(wk, LspTextDocumentSyncKindFull));

		obj completion_provider = make_obj(wk, obj_dict);
		obj trigger_characters = make_obj(wk, obj_array);
		obj_array_push(wk, trigger_characters, make_str(wk, "."));
		obj_dict_set(wk, completion_provider, make_str(wk, "triggerCharacters"), trigger_characters);
		obj_dict_set(wk, capabilities, make_str(wk, "completionProvider"), completion_provider);
		obj_dict_set(wk, capabilities, make_str(wk, "hoverProvider"), obj_bool_true);
		obj_dict_set(wk, capabilities, make_str(wk, "definitionProvider"), obj_bool_true);

		obj_dict_set(wk, result, make_str(wk, "capabilities"), capabilities);

		obj server_info = make_obj(wk, obj_dict);
		obj_dict_set(wk, server_info, make_str(wk, "name"), make_str(wk, "muon"));
		obj_dict_set(wk, server_info, make_str(wk, "version"), make_str(wk, muon_version.version));

		obj_dict_set(wk, result, make_str(wk, "serverInfo"), server_info);

		az_srv_respond(srv, wk, obj_dict_index_as_obj(wk, msg, "id"), result);
	} else if (str_eql(method, &STR("$/setTrace"))) {
		obj params = obj_dict_index_as_obj(wk, msg, "params");
		const struct str *value = obj_dict_index_as_str(wk, params, "value");
		if (str_eql(value, &STR("verbose"))) {
			srv->verbose = true;
		}
	} else if (str_eql(method, &STR("textDocument/didOpen"))) {
		obj params = obj_dict_index_as_obj(wk, msg, "params");
		obj text_document = obj_dict_index_as_obj(wk, params, "textDocument");
		const struct str *uri = obj_dict_index_as_str(wk, text_document, "uri");
		const struct str *content = obj_dict_index_as_str(wk, text_document, "text");

		az_srv_set_src_override(srv, wk, uri, content);
	} else if (str_eql(method, &STR("textDocument/didChange"))) {
		obj params = obj_dict_index_as_obj(wk, msg, "params");
		obj text_document = obj_dict_index_as_obj(wk, params, "textDocument");
		const struct str *uri = obj_dict_index_as_str(wk, text_document, "uri");
		obj content_changes = obj_dict_index_as_obj(wk, params, "contentChanges");
		obj change0 = obj_array_index(wk, content_changes, 0);
		const struct str *content = obj_dict_index_as_str(wk, change0, "text");

		az_srv_set_src_override(srv, wk, uri, content);
	} else if (str_eql(method, &STR("textDocument/didSave"))) {
		obj params = obj_dict_index_as_obj(wk, msg, "params");
		obj text_document = obj_dict_index_as_obj(wk, params, "textDocument");
		const struct str *uri = obj_dict_index_as_str(wk, text_document, "uri");

		az_srv_set_src_override(srv, wk, uri, 0);
	} else if (str_eql(method, &STR("textDocument/hover"))) {
		srv->req.type = az_srv_req_type_hover;
		srv->req.id = obj_dict_index_as_obj(wk, msg, "id");
		srv->req.result = make_str(wk, "");
		az_srv_handle_push_breakpoint_from_msg(srv, wk, msg);
	} else if (str_eql(method, &STR("textDocument/definition"))) {
		srv->req.type = az_srv_req_type_definition;
		srv->req.id = obj_dict_index_as_obj(wk, msg, "id");
		az_srv_handle_push_breakpoint_from_msg(srv, wk, msg);
	} else if (str_eql(method, &STR("textDocument/completion"))) {
		srv->req.type = az_srv_req_type_completion;
		srv->req.id = obj_dict_index_as_obj(wk, msg, "id");
		srv->req.result = make_obj(wk, obj_array);
		az_srv_handle_push_breakpoint_from_msg(srv, wk, msg);
	}

	TracyCZoneAutoE;
}

static obj
az_srv_push_completion(struct az_srv *srv,
	struct workspace *wk,
	obj label,
	enum LspCompletionItemKind kind,
	obj insert_text)
{
	obj c;
	obj_array_for(wk, srv->req.result, c) {
		if (str_eql(obj_dict_index_as_str(wk, c, "label"), get_str(wk, label))
			&& obj_dict_index_as_number(wk, c, "kind") == kind) {
			// Skip duplicates
			return 0;
		}
	}

	c = make_obj(wk, obj_dict);
	obj_dict_set(wk, c, make_str(wk, "label"), label);
	obj_dict_set(wk, c, make_str(wk, "kind"), make_number(wk, kind));
	if (insert_text) {
		obj_dict_set(wk, c, make_str(wk, "insertText"), insert_text);
	}

	obj_array_push(wk, srv->req.result, c);
	return c;
}

static void
az_srv_get_dict_completions(struct az_srv *srv,
	struct workspace *wk,
	obj dict,
	const struct str *prefix,
	enum LspCompletionItemKind kind)
{
	obj name, val;
	obj_dict_for(wk, dict, name, val) {
		(void)val;
		if (str_startswith(get_str(wk, name), prefix)) {
			az_srv_push_completion(srv, wk, name, kind, 0);
		}
	}
}

enum az_srv_get_func_completions_flag {
	az_srv_get_func_completions_flag_module = 1 << 0,
};

static obj
az_srv_func_proto_string(struct workspace *wk, obj f)
{
	obj name = 0, return_type = 0, posargs = 0, kwargs = 0, arg;
	obj_dict_index_str(wk, f, "name", &name);
	obj_dict_index_str(wk, f, "type", &return_type);
	obj_dict_index_str(wk, f, "posargs", &posargs);
	obj_dict_index_str(wk, f, "kwargs", &kwargs);

	obj sig = make_obj(wk, obj_array);

	if (posargs) {
		obj_array_for(wk, posargs, arg) {
			obj_array_push(wk, sig, obj_dict_index_as_obj(wk, arg, "type"));
		}
	}

	if (kwargs) {
		obj_array_for(wk, kwargs, arg) {
			obj_array_push(wk,
				sig,
				make_strf(wk,
					"%s %s:",
					obj_dict_index_as_str(wk, arg, "name")->s,
					obj_dict_index_as_str(wk, arg, "type")->s));
		}
	}

	obj joined;
	obj_array_join(wk, false, sig, make_str(wk, ", "), &joined);

	obj proto = make_strf(wk, "%s(%s)", get_str(wk, name)->s, get_str(wk, joined)->s);
	if (return_type) {
		str_appf(wk, &proto, " -> %s", get_str(wk, return_type)->s);
	}

	return proto;
}

static void
az_srv_push_func_completion(struct az_srv *srv, struct workspace *wk, enum LspCompletionItemKind kind, obj f)
{
	obj name = 0, desc = 0;
	obj_dict_index_str(wk, f, "name", &name);
	obj_dict_index_str(wk, f, "desc", &desc);

	obj proto = az_srv_func_proto_string(wk, f);

	obj c;
	if ((c = az_srv_push_completion(srv, wk, proto, kind, name))) {
		if (desc) {
			obj_dict_set(wk, c, make_str(wk, "documentation"), desc);
		}
	}
}

static void
az_srv_get_func_completions(struct az_srv *srv,
	struct workspace *wk,
	uint32_t type_or_module,
	const struct func_impl_group *impl_group,
	const struct str *prefix,
	enum az_srv_get_func_completions_flag flags)
{
	const bool is_module_func = flags & az_srv_get_func_completions_flag_module;
	enum LspCompletionItemKind kind = (type_or_module == obj_null || is_module_func) ?
						  LspCompletionItemKindFunction :
						  LspCompletionItemKindMethod;

	enum language_mode modes[2] = { wk->vm.lang_mode };
	uint32_t modes_len = 1;

	if (wk->vm.lang_mode == language_extended) {
		modes[0] = language_external;
		modes[1] = language_internal;
		modes_len = 2;
	}

	uint32_t mode;
	for (mode = 0; mode < modes_len; ++mode) {
		const enum language_mode m = modes[mode];

		if (!impl_group[m].len) {
			continue;
		}

		uint32_t i;
		for (i = 0; i < impl_group[m].len; ++i) {
			const struct func_impl *impl = &impl_group[m].impls[i];
			if (str_startswith(&STRL(impl->name), prefix)) {
				obj f;
				if (is_module_func) {
					f = dump_module_function_native(wk, type_or_module, impl);
				} else {
					f = dump_function_native(wk, type_or_module, impl);
				}

				az_srv_push_func_completion(srv, wk, kind, f);
			}
		}
	}
}

static bool
az_srv_get_kwarg_completions(struct workspace *wk, struct args_norm posargs[], struct args_kw kwargs[])
{
	if (!kwargs) {
		return false;
	}

	struct az_srv *srv = wk->vm.dbg_state.usr_ctx;

	uint32_t i;
	for (i = 0; kwargs[i].key; ++i) {
		az_srv_push_completion(srv, wk, make_str(wk, kwargs[i].key), LspCompletionItemKindKeyword, 0);
	}

	return false;
}

enum az_srv_break_type {
	az_srv_break_type_constant,
	az_srv_break_type_member,
	az_srv_break_type_native_call,
};

struct az_srv_break_info {
	enum az_srv_break_type type;
	union {
		struct {
			obj ident;
		} constant;
		struct {
			obj self;
			obj ident;
		} member;
		struct {
			uint32_t nargs, nkwargs, idx;
		} native_call;
	} dat;
};

static void
az_srv_get_completions(struct az_srv *srv, struct workspace *wk, struct az_srv_break_info *info)
{
	switch (info->type) {
	case az_srv_break_type_constant: {
		const struct str *prefix = get_str(wk, info->dat.constant.ident);

		obj local_scope;
		obj_array_for(wk, wk->vm.scope_stack, local_scope) {
			uint32_t local_scope_len = get_obj_array(wk, local_scope)->len;
			if (local_scope_len > 1) {
				int32_t i;
				for (i = local_scope_len - 1; i >= 1; --i) {
					obj scope_group;
					scope_group = obj_array_index(wk, local_scope, i);
					obj scope = obj_array_get_tail(wk, scope_group);

					az_srv_get_dict_completions(
						srv, wk, scope, prefix, LspCompletionItemKindVariable);
				}
			}

			obj base = obj_array_index(wk, local_scope, 0);
			az_srv_get_dict_completions(srv, wk, base, prefix, LspCompletionItemKindVariable);
		}

		az_srv_get_func_completions(srv, wk, 0, func_lookup_group(0), prefix, 0);
		break;
	}
	case az_srv_break_type_member: {
		obj self = info->dat.member.self;
		const struct str *prefix = get_str(wk, info->dat.member.ident);

		enum obj_type t = get_obj_type(wk, self);
		if (t == obj_typeinfo) {
			type_tag t = get_obj_typeinfo(wk, self)->type;

			uint32_t i;
			for (i = 1; i <= tc_type_count; ++i) {
				type_tag tc = obj_type_to_tc_type(i);
				if ((t & tc) != tc) {
					continue;
				}

				az_srv_get_func_completions(srv, wk, i, func_lookup_group(i), prefix, 0);
			}
		} else if (t == obj_module) {
			az_srv_get_func_completions(srv, wk, t, func_lookup_group(t), prefix, 0);
			struct obj_module *m = get_obj_module(wk, self);
			if (!m->found) {
				return;
			}

			if (m->exports) {
				obj name, val;
				obj_dict_for(wk, m->exports, name, val) {
					if (!str_startswith(get_str(wk, name), prefix)) {
						continue;
					}

					// We don't use the module name here, it could be anything
					obj f = dump_module_function_capture(wk, "<module_name>", name, val);

					az_srv_push_func_completion(srv, wk, LspCompletionItemKindFunction, f);
				}
			} else {
				az_srv_get_func_completions(srv,
					wk,
					m->module,
					module_func_impl_groups[m->module],
					prefix,
					az_srv_get_func_completions_flag_module);
			}
		} else if ((wk->vm.lang_mode == language_internal || wk->vm.lang_mode == language_extended)
			   && t == obj_dict) {
			az_srv_get_dict_completions(srv, wk, self, prefix, LspCompletionItemKindProperty);
		} else {
			az_srv_get_func_completions(srv, wk, t, func_lookup_group(t), prefix, 0);
		}
		break;
	}
	case az_srv_break_type_native_call: {
		stack_push(&wk->stack, wk->vm.behavior.pop_args, az_srv_get_kwarg_completions);
		native_funcs[info->dat.native_call.idx].func(wk, 0, 0);
		stack_pop(&wk->stack, wk->vm.behavior.pop_args);
		break;
	}
	}
}

static void
az_srv_get_hover_info(struct az_srv *srv, struct workspace *wk, struct az_srv_break_info *info)
{
	switch (info->type) {
	case az_srv_break_type_constant: {
		obj res;
		if (wk->vm.behavior.get_variable(wk, get_str(wk, info->dat.constant.ident)->s, &res)) {
			srv->req.result = obj_type_to_typestr(wk, res);
		}
		break;
	}
	case az_srv_break_type_member: {
		break;
	}
	case az_srv_break_type_native_call: {
		obj f = dump_function_native(wk, 0, &native_funcs[info->dat.native_call.idx]);
		obj proto = az_srv_func_proto_string(wk, f);
		srv->req.result = proto;
		break;
	}
	}
}

static void
az_srv_get_definition_for_ip(struct az_srv *srv, struct workspace *wk, uint32_t ip)
{
	if (!ip) {
		return;
	}

	srv->req.result = make_obj(wk, obj_dict);

	struct source_location loc;
	struct source *src;
	vm_lookup_inst_location(&wk->vm, ip, &loc, &src);

	obj_dict_set(wk, srv->req.result, make_str(wk, "uri"), az_srv_path_to_uri(wk, &STRL(src->label)));
	obj_dict_set(wk, srv->req.result, make_str(wk, "range"), az_srv_range(wk, src, loc));
}

static void
az_srv_get_definition_info(struct az_srv *srv, struct workspace *wk, struct az_srv_break_info *info)
{
	switch (info->type) {
	case az_srv_break_type_constant: {
		struct az_assignment *a;
		if ((a = az_assign_lookup(wk, get_str(wk, info->dat.constant.ident)->s))) {
			az_srv_get_definition_for_ip(srv, wk, a->ip);
		}
		break;
	}
	case az_srv_break_type_member: {
		if (get_obj_type(wk, info->dat.member.self) == obj_module) {
			struct obj_module *m = get_obj_module(wk, info->dat.member.self);
			if (m->found && m->exports) {
				info->dat.member.self = m->exports;
			}
		}

		if (get_obj_type(wk, info->dat.member.self) == obj_dict) {
			uint32_t ip = az_dict_member_location_lookup_str(
				wk, info->dat.member.self, get_str(wk, info->dat.member.ident)->s);
			if (ip) {
				az_srv_get_definition_for_ip(srv, wk, ip);
			}
		}
		break;
	}
	case az_srv_break_type_native_call: {
		if (str_eql(&STRL(native_funcs[info->dat.native_call.idx].name), &STR("subdir"))) {
			if (info->dat.native_call.nargs == 1) {
				obj tgt = object_stack_peek(&wk->vm.stack, (info->dat.native_call.nkwargs * 2) + 1);
				if (get_obj_type(wk, tgt) == obj_string) {
					TSTR(tmp);
					path_copy(wk, &tmp, get_cstr(wk, current_project(wk)->cwd));
					path_push(wk, &tmp, get_cstr(wk, tgt));
					path_push(wk, &tmp, "meson.build");

					srv->req.result = make_obj(wk, obj_dict);
					obj_dict_set(wk, srv->req.result, make_str(wk, "uri"), az_srv_path_to_uri(wk, &TSTR_STR(&tmp)));
					obj range = make_obj(wk, obj_dict);
					obj_dict_set(wk, range, make_str(wk, "start"), az_srv_position(wk, 1, 1));
					obj_dict_set(wk, range, make_str(wk, "end"), az_srv_position(wk, 1, 1));
					obj_dict_set(wk, srv->req.result, make_str(wk, "range"), range);
				}
			}
		} else if (str_eql(&STRL(native_funcs[info->dat.native_call.idx].name), &STR("import"))) {
		}
		break;
	}
	}
}

static bool
az_srv_inst_seq_matches(struct workspace *wk, uint32_t ip, const uint8_t *seq, uint32_t seq_len)
{
	uint32_t seq_i = 0;
	for (; seq_i < seq_len && ip < wk->vm.code.len;) {
		uint32_t op = wk->vm.code.e[ip];
		if (op != seq[seq_i]) {
			return false;
		}

		ip += OP_WIDTH(op);
		++seq_i;
	}

	return seq_i == seq_len;
}

static void
az_srv_dbg_break_cb(struct workspace *wk)
{
	struct az_srv *srv = wk->vm.dbg_state.usr_ctx;
	uint32_t ip = wk->vm.ip;

#if 0
	L("hit breakpoint");
	for (uint32_t i = ip; i < wk->vm.code.len; i += OP_WIDTH(wk->vm.code.e[i])) {
		L("%s", vm_dis_inst(wk, wk->vm.code.e, i));
	}
	L("---");
#endif

	struct az_srv_break_info info = { 0 };

	if (az_srv_inst_seq_matches(wk, ip, (uint8_t[]){ op_constant, op_load }, 2)) {
		info.type = az_srv_break_type_constant;
		++ip;
		info.dat.constant.ident = vm_get_constant(wk->vm.code.e, &ip);
	} else if (az_srv_inst_seq_matches(wk, ip, (uint8_t[]){ op_member }, 1)) {
		info.type = az_srv_break_type_member;
		++ip;
		info.dat.member.self = object_stack_peek(&wk->vm.stack, 1);
		info.dat.member.ident = vm_get_constant(wk->vm.code.e, &ip);
	} else if (az_srv_inst_seq_matches(wk, ip, (uint8_t[]){ op_call_native }, 1)) {
		info.type = az_srv_break_type_native_call;
		++ip;
		info.dat.native_call.nargs = vm_get_constant(wk->vm.code.e, &ip);
		info.dat.native_call.nkwargs = vm_get_constant(wk->vm.code.e, &ip);
		info.dat.native_call.idx = vm_get_constant(wk->vm.code.e, &ip);
	} else {
		return;
	}

	switch (srv->req.type) {
	case az_srv_req_type_completion: az_srv_get_completions(srv, wk, &info); break;
	case az_srv_req_type_hover: az_srv_get_hover_info(srv, wk, &info); break;
	case az_srv_req_type_definition: az_srv_get_definition_info(srv, wk, &info); break;
	}
}

static bool
az_srv_cfg_parse(struct workspace *wk, struct az_opts *dest, const char *cfg_path)
{
	const struct ini_cfg_key keys[] = {
		{ "debug_log", ini_cfg_type_bool, offsetof(struct az_opts, lsp.debug_log) },
		{ "wait_for_debugger", ini_cfg_type_bool, offsetof(struct az_opts, lsp.wait_for_debugger) },
		0,
	};

	return ini_cfg_parse(wk, cfg_path, keys, 0, dest);
}

bool
analyze_server(struct workspace *srv_wk, struct az_opts *cmdline_opts)
{
	log_set_file(srv_wk, stderr);

	{
		TSTR(config);
		if (fs_path_config_base(srv_wk, &config, false)) {
			path_push(srv_wk, &config, "lsp.ini");
			if (fs_file_exists(config.buf)) {
				if (!az_srv_cfg_parse(srv_wk, cmdline_opts, config.buf)) {
					return false;
				}
			}
		}
	}

	if (cmdline_opts->lsp.wait_for_debugger) {
		LOG_I("muon lsp waiting for debugger...");
		while (!os_is_debugger_attached()) { }
	}

	FILE *debug_log = 0;
	if (cmdline_opts->lsp.debug_log) {
		TSTR(path);
		if (fs_path_state_base(srv_wk, &path, true)) {
			char file[256];
			snprintf(file, sizeof(file), "lsp.%d.log", os_get_pid());
			path_push(srv_wk, &path, file);

			if ((debug_log = fs_fopen(path.buf, "wb"))) {
				log_set_debug_file(debug_log);
			}
		}
	}

	struct az_srv srv = {
		.transport = {
			.in = 0, // STDIN_FILENO
			.out = stdout,
		},
		.wk = srv_wk,
		.diagnostics_to_clear = make_obj(srv_wk, obj_array),
	};

	analyze_opts_init(srv.wk, &srv.opts);
	srv.opts.enabled_diagnostics = cmdline_opts->enabled_diagnostics;

	struct arena a, a_scratch;
	arena_init(&a,);
	arena_init(&a_scratch,);

	bool ok = true;

	LOG_I("muon %s%s%s lsp listening...", muon_version.version, *muon_version.vcs_tag ? "-" : "", muon_version.vcs_tag);

	TSTR(in_buf);
	srv.transport.in_buf = &in_buf;

	while (true) {
		TracyCFrameMark;

		struct workspace wk = { .a = &a, .a_scratch = &a_scratch };
		workspace_init_bare(&wk, &a, &a_scratch);

		srv.should_analyze = false;
		srv.req.id = srv.req.result = srv.req.type = 0;

		obj msg;
		switch (az_srv_read(&srv, &wk, &msg)) {
		case az_srv_read_result_err:
			ok = false;
			goto shutdown;
		case az_srv_read_result_eof:
			goto shutdown;
		case az_srv_read_result_ok:
			break;
		}

		workspace_scratch_begin(srv_wk);

		obj_lprintf(&wk, log_debug, "<<< %#o\n", msg);

		az_srv_handle(&srv, &wk, msg);

		if (srv.should_analyze) {
			struct az_opts opts = { 0 };
			opts.file_override = make_obj(&wk, obj_dict);
			opts.file_override_src = srv.opts.file_override_src;
			opts.enabled_diagnostics = srv.opts.enabled_diagnostics;
			opts.replay_opts = error_diagnostic_store_replay_prepare_only;

			if (srv.req.id && srv.req.type == az_srv_req_type_completion) {
				opts.relaxed_parse = true;
			}

			{
				obj path, idx;
				obj_dict_for(srv.wk, srv.opts.file_override, path, idx) {
					obj_dict_set(&wk, opts.file_override, str_clone(srv.wk, &wk, path), idx);
				}
			}

			wk.vm.dbg_state.break_cb = az_srv_dbg_break_cb;
			wk.vm.dbg_state.usr_ctx = &srv;

			do_analyze(&wk, &opts);

			if (srv.req.id) {
				switch (srv.req.type) {
				case az_srv_req_type_definition:
				case az_srv_req_type_completion:
					az_srv_respond(&srv, &wk, srv.req.id, srv.req.result);
					break;
				case az_srv_req_type_hover: {
					obj result = make_obj(&wk, obj_dict);
					obj_dict_set(&wk, result, make_str(&wk, "contents"), srv.req.result);
					az_srv_respond(&srv, &wk, srv.req.id, result);
					break;
				}
				}
			} else {
				az_srv_all_diagnostics(&srv, &wk);
			}

		}
		log_flush();

		workspace_scratch_end(srv_wk);

		ar_clear(&a);
		ar_clear(&a_scratch);
	}

shutdown:

	LOG_I("muon lsp shutting down");

	if (debug_log) {
		fs_fclose(debug_log);
		log_set_debug_file(0);
	}

	ar_destroy(&a);
	ar_destroy(&a_scratch);

	return ok;
}

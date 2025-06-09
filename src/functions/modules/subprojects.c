/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "functions/modules/subprojects.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "platform/path.h"
#include "platform/timer.h"
#include "tracy.h"
#include "util.h"
#include "wrap.h"

struct subprojects_foreach_ctx {
	subprojects_foreach_cb cb;
	struct subprojects_common_ctx *usr_ctx;
	struct workspace *wk;
	const char *subprojects_dir;
};

static const char *
subprojects_dir(struct workspace *wk)
{
	TSTR(path);
	path_join(wk,
		&path,
		get_cstr(wk, current_project(wk)->source_root),
		get_cstr(wk, current_project(wk)->subprojects_dir));

	return get_str(wk, tstr_into_str(wk, &path))->s;
}

static enum iteration_result
subprojects_foreach_iter(void *_ctx, const char *name)
{
	struct subprojects_foreach_ctx *ctx = _ctx;
	uint32_t len = strlen(name);
	TSTR(path);

	if (len <= 5 || strcmp(&name[len - 5], ".wrap") != 0) {
		return ir_cont;
	}

	path_join(ctx->wk, &path, subprojects_dir(ctx->wk), name);

	if (!fs_file_exists(path.buf)) {
		return ir_cont;
	}

	return ctx->cb(ctx->wk, ctx->usr_ctx, path.buf);
}

bool
subprojects_foreach(struct workspace *wk, obj list, struct subprojects_common_ctx *usr_ctx, subprojects_foreach_cb cb)
{
	if (list) {
		bool res = true;
		TSTR(wrap_file);

		obj v;
		obj_array_for(wk, list, v) {
			const char *name = get_cstr(wk, v);
			path_join(wk, &wrap_file, subprojects_dir(wk), name);

			tstr_pushs(wk, &wrap_file, ".wrap");

			if (!fs_file_exists(wrap_file.buf)) {
				LOG_E("wrap file for '%s' not found", name);
				res = false;
				break;
			}

			if (cb(wk, usr_ctx, wrap_file.buf) == ir_err) {
				res = false;
				break;
			}
		}

		return res;
	} else if (fs_dir_exists(subprojects_dir(wk))) {
		struct subprojects_foreach_ctx ctx = {
			.cb = cb,
			.usr_ctx = usr_ctx,
			.wk = wk,
		};

		return fs_dir_foreach(subprojects_dir(wk), &ctx, subprojects_foreach_iter);
	}

	return true;
}

static enum iteration_result
subprojects_gather_iter(struct workspace *wk, struct subprojects_common_ctx *ctx, const char *path)
{
	struct wrap_handle_ctx wrap_ctx = {
		.path = get_str(wk, make_str(wk, path))->s,
		.opts = {
			.allow_download = true,
			.subprojects = subprojects_dir(wk),
		},
	};

	arr_push(&ctx->handlers, &wrap_ctx);

	return ir_cont;
}

struct subprojects_process_opts {
	uint32_t job_count;
	enum wrap_handle_mode wrap_mode;
	const char *subprojects_dir;
	obj *res;
	bool single_file;
	bool progress_bar;
};

struct subprojects_process_progress_decorate_ctx {
	uint32_t prev_list_len;
	struct subprojects_common_ctx *ctx;
};

struct subprojects_process_progress_decorate_elem {
	struct wrap_handle_ctx *wrap_ctx;
	float dur;
};

static int32_t
subprojects_process_progress_decorate_sort_compare(const void *_a, const void *_b)
{
	const struct subprojects_process_progress_decorate_elem *a = _a, *b = _b;
	return a->dur > b->dur ? -1 : 1;
}

static void
subprojects_process_progress_decorate(void *_ctx, uint32_t width)
{
	struct subprojects_process_progress_decorate_ctx *decorate_ctx = _ctx;
	struct subprojects_common_ctx *ctx = decorate_ctx->ctx;

	float elapsed = timer_read(&ctx->duration);
	if (elapsed < 1) {
		log_raw("\r");
		return;
	}
	log_raw("\n");

	struct wrap_handle_ctx *wrap_ctx;
	struct subprojects_process_progress_decorate_elem list[32] = { 0 };
	uint32_t list_len = 0;

	uint32_t i;
	for (i = 0; i < ctx->handlers.len; ++i) {
		wrap_ctx = arr_get(&ctx->handlers, i);

		if (wrap_ctx->sub_state == wrap_handle_sub_state_pending) {
			continue;
		} else if (wrap_ctx->sub_state == wrap_handle_sub_state_collected) {
			continue;
		}

		list[list_len].wrap_ctx = wrap_ctx;
		list[list_len].dur = timer_read(&wrap_ctx->duration);
		++list_len;

		if (list_len >= ARRAY_LEN(list)) {
			break;
		}
	}

	qsort(list,
		list_len,
		sizeof(struct subprojects_process_progress_decorate_elem),
		subprojects_process_progress_decorate_sort_compare);

	for (i = 0; i < list_len; ++i) {
		char buf[512] = { 0 };
		uint32_t buf_i = 0;

		snprintf_append(buf, &buf_i, "%6.2fs " CLR(c_magenta, c_bold) "%-20.20s" CLR(0) " " CLR(c_blue) "%-9s" CLR(0),
			list[i].dur,
			list[i].wrap_ctx->wrap.name.buf,
			wrap_handle_state_to_s(list[i].wrap_ctx->prev_state));

		switch (list[i].wrap_ctx->sub_state) {
		case wrap_handle_sub_state_fetching: {
			snprintf_append(buf, &buf_i, "%s", " fetching");
			const int64_t total = list[i].wrap_ctx->fetch_ctx.total,
				      dl = list[i].wrap_ctx->fetch_ctx.downloaded;
			if (total && dl && dl <= total) {
				snprintf_append(buf, &buf_i, " %3.0f%%", 100.0 * (double)dl / (double)total);
			}
			break;
		}
		case wrap_handle_sub_state_extracting: {
			snprintf_append(buf, &buf_i, "%s", " extracting");
			break;
		}
		case wrap_handle_sub_state_running_cmd: {
			struct tstr *out = &list[i].wrap_ctx->cmd_ctx.err;

			int32_t j, end;

			if (out->len) {
				j = out->len - 1;
				if (out->buf[j] == '\r' || out->buf[j] == '\n') {
					j--;
				}
				end = j;

				for (; j >= 1; --j) {
					if (out->buf[j] == '\r' || out->buf[j] == '\n') {
						++j;
						break;
					}
				}

				int32_t len = end - j;
				snprintf_append(buf, &buf_i, " %.*s", len, out->buf + j);
			}
			break;
		}
		default: break;
		}

		for (uint32_t j = 0, w = 0; j < buf_i; ++j) {
			if (buf[j] == '\033') {
				while (j < buf_i && buf[j] != 'm') {
					++j;
				}
				continue;
			}
			++w;

			if (w >= width)  {
				buf[j] = 0;
				break;
			}
		}

		log_raw("%s\033[K\n", buf);
	}
	for (; i < decorate_ctx->prev_list_len; ++i) {
		log_raw("\033[K\n");
	}

	log_raw("\033[%dA", MAX(list_len, decorate_ctx->prev_list_len) + 1);
	decorate_ctx->prev_list_len = list_len;
}

static obj
wrap_to_obj(struct workspace *wk, struct wrap *wrap)
{
	char *t = "file";
	if (wrap->type == wrap_type_git) {
		t = "git ";
	}

	obj d = make_obj(wk, obj_dict);
	obj_dict_set(wk, d, make_str(wk, "name"), make_str(wk, wrap->name.buf));
	obj_dict_set(wk, d, make_str(wk, "type"), make_str(wk, t));
	obj_dict_set(wk, d, make_str(wk, "path"), tstr_into_str(wk, &wrap->dest_dir));
	return d;
}

static bool
subprojects_process(struct workspace *wk, obj list, struct subprojects_process_opts *opts)
{
	// Init ctx
	struct subprojects_common_ctx ctx = {
		.res = opts->res
	};
	arr_init(&ctx.handlers, 8, sizeof(struct wrap_handle_ctx));
	*ctx.res = make_obj(wk, obj_array);

	// Gather subprojects
	if (opts->single_file) {
		struct wrap_handle_ctx wrap_ctx = {
			.path = get_str(wk, list)->s,
			.opts = {
				.allow_download = true,
				.subprojects = opts->subprojects_dir,
			},
		};
		arr_push(&ctx.handlers, &wrap_ctx);
	} else {
		subprojects_foreach(wk, list, &ctx, subprojects_gather_iter);
	}

	// Progress bar setup
	log_progress_push_state(wk);
	if (opts->progress_bar) {
		log_progress_enable();
		log_progress_push_level(0, ctx.handlers.len);
	}
	struct subprojects_process_progress_decorate_ctx decorate_ctx = { .ctx = &ctx };
	struct log_progress_style log_progress_style = {
		.show_count = true,
		.decorate = subprojects_process_progress_decorate,
		.usr_ctx = &decorate_ctx,
		.dont_disable_on_error = true,
	};
	log_progress_set_style(&log_progress_style);

	uint32_t i;
	uint32_t cnt_complete = 0, cnt_failed = 0, cnt_running;
	struct wrap_handle_ctx *wrap_ctx;

	for (i = 0; i < ctx.handlers.len; ++i) {
		wrap_ctx = arr_get(&ctx.handlers, i);

		wrap_ctx->opts.mode = opts->wrap_mode;
	}

	wrap_handle_async_start(wk);
	timer_start(&ctx.duration);

	while (cnt_complete < ctx.handlers.len) {
		float loop_start = timer_read(&ctx.duration);
		cnt_running = 0;

		for (i = 0; i < ctx.handlers.len; ++i) {
			wrap_ctx = arr_get(&ctx.handlers, i);

			if (wrap_ctx->sub_state == wrap_handle_sub_state_complete) {
				++cnt_complete;
				if (opts->wrap_mode == wrap_handle_mode_update && wrap_ctx->wrap.updated) {
					LOG_I(CLR(c_green) "[%3d/%3d] updated" CLR(0) " %s",
						cnt_complete,
						ctx.handlers.len,
						wrap_ctx->wrap.name.buf);
				}
				wrap_ctx->sub_state = wrap_handle_sub_state_collected;
			}
			if (wrap_ctx->sub_state == wrap_handle_sub_state_collected) {
				continue;
			}

			wrap_ctx->ok = true;
			++cnt_running;

			if (cnt_running > opts->job_count) {
				break;
			}

			TracyCZoneN(tctx_1, "wrap_handle_async", true);

			if (!wrap_handle_async(wk, wrap_ctx->path, wrap_ctx)) {
				wrap_ctx->sub_state = wrap_handle_sub_state_collected;
				if (wrap_ctx->wrap.name.len) {
					LOG_I(CLR(c_red) "failed" CLR(0) " %s", wrap_ctx->wrap.name.buf);
				}
				wrap_ctx->ok = false;
				++cnt_failed;
				++cnt_complete;
			}

			TracyCZoneEnd(tctx_1);
		}

		log_progress_subval(wk, cnt_complete, cnt_complete + cnt_running);

		float loop_dur_ns = (timer_read(&ctx.duration) - loop_start) * 1e9;
		if (loop_dur_ns < ((double)SLEEP_TIME/10.0)) {
			timer_sleep(((double)SLEEP_TIME/10.0) - loop_dur_ns);
		}
	}

	for (i = 0; i < ctx.handlers.len; ++i) {
		wrap_ctx = arr_get(&ctx.handlers, i);

		if (!wrap_ctx->ok) {
			continue;
		}

		obj d = wrap_to_obj(wk, &wrap_ctx->wrap);

		switch (opts->wrap_mode) {
		case wrap_handle_mode_check_dirty: {
			obj_dict_set(wk, d, make_str(wk, "outdated"), make_obj_bool(wk, wrap_ctx->wrap.outdated));
			obj_dict_set(wk, d, make_str(wk, "dirty"), make_obj_bool(wk, wrap_ctx->wrap.dirty));
			break;
		}
		default: break;
		}

		obj_array_push(wk, *opts->res, d);

		wrap_destroy(&wrap_ctx->wrap);
	}

	wrap_handle_async_end(wk);

	arr_destroy(&ctx.handlers);

	if (opts->progress_bar) {
		log_progress_disable();
	}
	log_progress_pop_state(wk);

	return cnt_failed == 0;
}

static bool
func_subprojects_update(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ TYPE_TAG_LISTIFY | tc_string, .optional = true, .desc = "A list of subprojects to operate on." },
		ARG_TYPE_NULL,
	};

	if (!pop_args(wk, an, 0)) {
		return false;
	}

	return subprojects_process(wk,
		an[0].val,
		&(struct subprojects_process_opts){
			.wrap_mode = wrap_handle_mode_update,
			.job_count = 8,
			.progress_bar = true,
			.res = res,
		});
}

static int32_t
subprojects_array_sort_func(struct workspace *wk, void *_ctx, obj a, obj b)
{
	const struct str *a_name = obj_dict_index_as_str(wk, a, "name");
	const struct str *b_name = obj_dict_index_as_str(wk, b, "name");

	return strcmp(a_name->s, b_name->s);
}

static bool
func_subprojects_list(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ TYPE_TAG_LISTIFY | tc_string, .optional = true, .desc = "A list of subprojects to operate on." },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_print,
	};
	struct args_kw akw[] = {
		[kw_print]
		= { "print", tc_bool, .desc = "Print out a formatted list of subprojects as well as returning it." },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (!subprojects_process(wk,
		    an[0].val,
		    &(struct subprojects_process_opts){
			    .wrap_mode = wrap_handle_mode_check_dirty,
			    .job_count = 4,
			    .res = res,
		    })) {
		return false;
	}

	{
		obj sorted;
		obj_array_sort(wk, 0, *res, subprojects_array_sort_func, &sorted);
		*res = sorted;
	}

	if (get_obj_bool_with_default(wk, akw[kw_print].val, false)) {
		obj d;
		obj_array_for(wk, *res, d) {
			const struct str *name = obj_dict_index_as_str(wk, d, "name");
			const struct str *type = obj_dict_index_as_str(wk, d, "type");

			const char *t_clr = CLR(c_blue);
			if (str_eql(type, &STR("git"))) {
				t_clr = CLR(c_magenta);
			}

			LLOG_I("[%s%s%s] %s ", t_clr, type->s, CLR(0), name->s);

			if (obj_dict_index_as_bool(wk, d, "outdated")) {
				log_plain(log_info, CLR(c_green) "U" CLR(0));
			}
			if (obj_dict_index_as_bool(wk, d, "dirty")) {
				log_plain(log_info, "*");
			}

			log_plain(log_info, "\n");
		}
	}

	return true;
}

static enum iteration_result
subprojects_clean_iter(struct workspace *wk, struct subprojects_common_ctx *ctx, const char *path)
{
	struct wrap wrap = { 0 };
	if (!wrap_parse(wk, subprojects_dir(wk), path, &wrap)) {
		goto cont;
	}

	bool can_clean = wrap.type == wrap_type_git || (wrap.type == wrap_type_file && wrap.fields[wf_source_url]);

	if (!can_clean) {
		goto cont;
	}

	if (!fs_dir_exists(wrap.dest_dir.buf)) {
		goto cont;
	}

	if (ctx->force) {
		LOG_I("removing %s", wrap.dest_dir.buf);
		fs_rmdir_recursive(wrap.dest_dir.buf, true);
		fs_rmdir(wrap.dest_dir.buf, true);

		obj_array_push(wk, *ctx->res, make_str(wk, wrap.name.buf));
	} else {
		LOG_I("would remove %s", wrap.dest_dir.buf);
	}

	wrap_destroy(&wrap);

cont:
	return ir_cont;
}

static bool
func_subprojects_clean(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ TYPE_TAG_LISTIFY | tc_string, .optional = true, .desc = "A list of subprojects to operate on." },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_force,
	};
	struct args_kw akw[] = {
		[kw_force] = { "force", tc_bool, .desc = "Force the operation." },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	*res = make_obj(wk, obj_array);
	struct subprojects_common_ctx ctx = {
		.force = get_obj_bool_with_default(wk, akw[kw_force].val, false),
		.print = true,
		.res = res,
	};

	return subprojects_foreach(wk, an[0].val, &ctx, subprojects_clean_iter);
}

static bool
func_subprojects_fetch(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ tc_string, .desc = "The wrap file to fetch." },
		{ tc_string, .optional = true, .desc = "The directory to fetch into" },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_force,
	};
	struct args_kw akw[] = {
		[kw_force] = { "force", tc_bool, .desc = "Force the operation." },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	return subprojects_process(wk,
		an[0].val,
		&(struct subprojects_process_opts){
			.wrap_mode = wrap_handle_mode_update,
			.job_count = 1,
			.progress_bar = true,
			.subprojects_dir = an[1].set ? get_cstr(wk, an[1].val) : path_cwd(),
			.single_file = true,
			.res = res,
		});
}

static bool
func_subprojects_load_wrap(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ tc_string, .desc = "The wrap file to load." },
		ARG_TYPE_NULL,
	};
	if (!pop_args(wk, an, 0)) {
		return false;
	}

	struct wrap wrap = { 0 };
	if (!wrap_parse(wk, ".", get_cstr(wk, an[0].val), &wrap)) {
		return false;
	}

	*res = wrap_to_obj(wk, &wrap);

	wrap_destroy(&wrap);
	return true;
}

const struct func_impl impl_tbl_module_subprojects[] = {
	{ "update", func_subprojects_update, .flags = func_impl_flag_sandbox_disable, .desc = "Update subprojects with .wrap files" },
	{ "list",
		func_subprojects_list,
		tc_array,
		.flags = func_impl_flag_sandbox_disable,
		.desc = "List subprojects with .wrap files and their status." },
	{ "clean", func_subprojects_clean, .flags = func_impl_flag_sandbox_disable, .desc = "Clean subprojects with .wrap files" },
	{ "fetch", func_subprojects_fetch, .flags = func_impl_flag_sandbox_disable, .desc = "Fetch a project using a .wrap file" },
	{ "load_wrap", func_subprojects_load_wrap, .flags = func_impl_flag_sandbox_disable, .desc = "Load a wrap file into a dict" },
	{ NULL, NULL },
};

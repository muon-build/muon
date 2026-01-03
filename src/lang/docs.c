/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "embedded.h"
#include "error.h"
#include "functions/modules.h"
#include "lang/docs.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"
#include "version.h"

static struct {
	struct arr sigs;
	obj func;
	struct meson_doc_entry_func *meson_doc_entry;

	enum {
		function_sig_dump_special_func_none,
		function_sig_dump_special_func_build_tgt,
	} special_func;
} function_sig_dump;

static const bool dump_docs_warn_missing = false;

struct meson_doc_entry_common {
	const char *name, *description, *type;
};

struct meson_doc_entry_func {
	struct meson_doc_entry_common common;
	uint32_t posargs_start, posargs_len;
	uint32_t kwargs_start, kwargs_len;
};

struct meson_doc_entry_arg {
	struct meson_doc_entry_common common;
	bool optional, glob;
};

#ifdef HAVE_MESON_DOCS_H
#include "meson_docs.h"
#else
struct meson_doc_entry_func *meson_doc_root[obj_type_count] = { 0 };
struct meson_doc_entry_arg meson_doc_posargs[] = { 0 };
struct meson_doc_entry_arg meson_doc_kwargs[] = { 0 };
#endif

static int32_t
arr_sort_by_string(const void *a, const void *b, void *_ctx)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

struct meson_doc_entry_arg *
meson_doc_lookup_function_kwarg(const struct meson_doc_entry_func *meson_doc_entry, const char *key)
{
	struct meson_doc_entry_arg *args = &meson_doc_kwargs[meson_doc_entry->kwargs_start];
	uint32_t i;
	for (i = 0; i < meson_doc_entry->kwargs_len; ++i) {
		if (strcmp(args[i].common.name, key) == 0) {
			return &args[i];
		}
	}

	return 0;
}

struct meson_doc_entry_func *
meson_doc_lookup_function(enum obj_type t, const char *name)
{
	uint32_t j;
	for (j = 0; meson_doc_root[t][j].common.name; ++j) {
		if (strcmp(meson_doc_root[t][j].common.name, name) == 0) {
			return &meson_doc_root[t][j];
		}
	}

	return 0;
}

static obj
dump_function_arg(struct workspace *wk, struct args_norm *an, uint32_t an_idx, struct args_kw *kw)
{
	obj dict;
	dict = make_obj(wk, obj_dict);

	obj name = 0;
	const char *desc = 0;
	bool extension = false;
	if (an) {
		if (an->desc) {
			desc = an->desc;
		} else if (function_sig_dump.meson_doc_entry) {
			if (an_idx >= function_sig_dump.meson_doc_entry->posargs_len) {
				if (dump_docs_warn_missing) {
					LOG_W("missing documentation for %s posarg %d",
						function_sig_dump.meson_doc_entry->common.name,
						an_idx);
				}
			} else {
				struct meson_doc_entry_arg *arg
					= &meson_doc_posargs[function_sig_dump.meson_doc_entry->posargs_start + an_idx];
				desc = arg->common.description;
			}
		}

		name = make_strf(wk, "%d", an_idx);
	} else {
		if (kw->desc) {
			desc = kw->desc;
		} else if (function_sig_dump.meson_doc_entry) {
			struct meson_doc_entry_arg *arg;

			arg = meson_doc_lookup_function_kwarg(function_sig_dump.meson_doc_entry, kw->key);

			if (!arg && function_sig_dump.special_func == function_sig_dump_special_func_build_tgt) {
				arg = meson_doc_lookup_function_kwarg(
					meson_doc_lookup_function(obj_null, "build_target"), kw->key);
			}

			if (arg) {
				desc = arg->common.description;
			} else if (dump_docs_warn_missing) {
				LOG_W("missing documentation for %s kwarg %s",
					function_sig_dump.meson_doc_entry->common.name,
					kw->key);
			}
		}

		name = make_str(wk, kw->key);
		extension = kw->extension;
	}

	obj_dict_set(wk, dict, make_str(wk, "name"), name);
	obj_dict_set(wk, dict, make_str(wk, "type"), typechecking_type_to_str(wk, an ? an->type : kw->type));
	if (desc) {
		obj_dict_set(wk, dict, make_str(wk, "desc"), make_str(wk, desc));
	}

	if (extension) {
		obj_dict_set(wk, dict, make_str(wk, "extension"), obj_bool_true);
	}

	return dict;
}

static bool
dump_function_args_cb(struct workspace *wk, struct args_norm posargs[], struct args_kw kwargs[])
{
	uint32_t i;

	obj res = function_sig_dump.func;

	if (posargs) {
		obj arr;
		arr = make_obj(wk, obj_array);

		for (i = 0; posargs[i].type != ARG_TYPE_NULL; ++i) {
			obj_array_push(wk, arr, dump_function_arg(wk, &posargs[i], i, 0));
		}

		obj_dict_set(wk, res, make_str(wk, "posargs"), arr);
	}

	if (kwargs) {
		obj arr;
		arr = make_obj(wk, obj_array);

		struct arr kwargs_list;
		struct kwargs_list_elem {
			const char *key;
			uint32_t i;
		};
		arr_init(wk->a_scratch, &kwargs_list, 8, struct kwargs_list_elem);

		for (i = 0; kwargs[i].key; ++i) {
			arr_push(wk->a, &kwargs_list,
				&(struct kwargs_list_elem){
					.key = kwargs[i].key,
					.i = i,
				});
		}

		arr_sort(&kwargs_list, NULL, arr_sort_by_string);

		obj dict;
		dict = make_obj(wk, obj_dict);

		for (i = 0; i < kwargs_list.len; ++i) {
			struct kwargs_list_elem *elem = arr_get(&kwargs_list, i);
			struct args_kw kwarg = kwargs[elem->i];
			char buf[256];

			// This is a hack to convert e.g. cpp_args to <lang>_args
			if (function_sig_dump.special_func == function_sig_dump_special_func_build_tgt) {
#define E(lang, s)              \
	{                       \
		#lang #s, #lang \
	}
#define x(lang) E(lang, _args), E(lang, _static_args), E(lang, _shared_args), E(lang, _pch),
				const struct {
					const char *kw, *lang;
				} lang_kws[] = { FOREACH_COMPILER_EXPOSED_LANGUAGE(x) };
#undef x
#undef E

				bool found = false;
				uint32_t i;
				for (i = 0; i < ARRAY_LEN(lang_kws); ++i) {
					if (strcmp(kwarg.key, lang_kws[i].kw) == 0) {
						found = true;
						break;
					}
				}

				if (found) {
					snprintf(buf, sizeof(buf), "<lang>%s", kwarg.key + strlen(lang_kws[i].lang));
					kwarg.key = buf;
				}
			}

			obj _res;
			if (obj_dict_index_str(wk, dict, kwarg.key, &_res)) {
				continue;
			}

			obj_array_push(wk, arr, dump_function_arg(wk, 0, 0, &kwarg));
			obj_dict_set(wk, dict, make_str(wk, kwarg.key), obj_bool_true);
		}

		obj_dict_set(wk, res, make_str(wk, "kwargs"), arr);
	}

	return false;
}

struct dump_function_opts {
	const char *module;
	const struct func_impl *impl;
	obj capture;
	enum obj_type rcvr_t;
	bool module_func;
};

static obj
dump_function(struct workspace *wk, struct dump_function_opts *opts)
{
	function_sig_dump.func = make_obj(wk, obj_dict);
	obj res = function_sig_dump.func;

	obj_dict_set(wk, res, make_str(wk, "name"), make_str(wk, opts->impl->name));
	if (opts->module_func) {
		obj_dict_set(wk, res, make_str(wk, "module"), make_str(wk, opts->module));
	} else if (opts->rcvr_t) {
		obj_dict_set(wk, res, make_str(wk, "rcvr"), make_str(wk, obj_type_to_s(opts->rcvr_t)));
	}
	obj_dict_set(wk, res, make_str(wk, "type"), typechecking_type_to_str(wk, opts->impl->return_type));

	if (opts->impl->file) {
		obj_dict_set(wk, res, make_str(wk, "source_file"), make_str(wk, opts->impl->file));
		obj_dict_set(wk, res, make_str(wk, "source_line"), make_number(wk, opts->impl->line));
	}

	const char *desc = 0;
	if (opts->impl->desc) {
		desc = opts->impl->desc;
	} else if (function_sig_dump.meson_doc_entry) {
		desc = function_sig_dump.meson_doc_entry->common.description;
	}

	if (desc) {
		obj_dict_set(wk, res, make_str(wk, "desc"), make_str(wk, desc));
	} else if (dump_docs_warn_missing) {
		LOG_W("missing documentation for %s.%s",
			opts->module_func ? opts->module : obj_type_to_s(opts->rcvr_t),
			opts->impl->name);
	}

	if (opts->impl->flags & func_impl_flag_extension) {
		obj_dict_set(wk, res, make_str(wk, "extension"), obj_bool_true);
	}

	stack_push(&wk->stack, wk->vm.behavior.pop_args, dump_function_args_cb);
	if (opts->impl->func) {
		opts->impl->func(wk, 0, 0);
	} else {
		obj _res;
		vm_eval_capture(wk, opts->capture, 0, 0, &_res);
	}
	stack_pop(&wk->stack, wk->vm.behavior.pop_args);

	return res;
}

obj
dump_function_native(struct workspace *wk, enum obj_type t, const struct func_impl *impl)
{
	if (meson_doc_root[t] && wk->vm.lang_mode == language_external) {
		function_sig_dump.meson_doc_entry = meson_doc_lookup_function(t, impl->name);
	}

	if (strcmp(impl->name, "executable") || strcmp(impl->name, "build_target")
		|| strcmp(impl->name, "shared_library") || strcmp(impl->name, "static_library")
		|| strcmp(impl->name, "both_libraries")) {
		function_sig_dump.special_func = function_sig_dump_special_func_build_tgt;
	}

	obj res = dump_function(wk,
		&(struct dump_function_opts){
			.rcvr_t = t,
			.impl = impl,
		});

	function_sig_dump.meson_doc_entry = 0;
	function_sig_dump.special_func = function_sig_dump_special_func_none;

	return res;
}

obj
dump_module_function_native(struct workspace *wk, enum module module, const struct func_impl *impl)
{
	return dump_function(wk,
		&(struct dump_function_opts){
			.module_func = true,
			.module = module_info[module].path,
			.impl = impl,
		});
}

obj
dump_module_function_capture(struct workspace *wk, const char *module, obj name, obj o)
{
	struct obj_capture *capture = get_obj_capture(wk, o);

	struct vm_inst_location loc;
	vm_inst_location(wk, capture->func->def, &loc);

	TSTR(file);
	if (loc.embedded) {
		path_push(wk, &file, "src/script");
	}
	path_push(wk, &file, loc.file);

	struct func_impl impl = {
		.name = get_cstr(wk, name),
		.desc = capture->func->desc,
		.return_type = capture->func->return_type,
		.file = file.buf,
		.line = loc.line,
	};

	return dump_function(wk,
		&(struct dump_function_opts){
			.module_func = true,
			.module = module,
			.impl = &impl,
			.capture = o,
		});
}

static void
dump_function_docs_fn_key(struct workspace *wk, struct tstr *buf, obj fn)
{
	obj name = 0, module = 0, rcvr = 0;
	obj_dict_index_str(wk, fn, "name", &name);
	obj_dict_index_str(wk, fn, "module", &module);
	obj_dict_index_str(wk, fn, "rcvr", &rcvr);

	tstr_pushf(wk,
		buf,
		"%s%s%s",
		rcvr ? get_str(wk, rcvr)->s : "",
		module ? get_str(wk, module)->s : "",
		name ? get_str(wk, name)->s : "");

}

static void
dump_function_docs_push(struct workspace *wk, obj doc, obj fn, struct hash *map)
{
	TSTR(key);
	dump_function_docs_fn_key(wk, &key, fn);

	obj modes = 0;
	uint64_t *v = hash_get_strn(map, key.buf, key.len);
	if (v) {
		fn = *v;
		if (!(modes = obj_dict_index_as_obj(wk, fn, "modes"))) {
			UNREACHABLE;
		}
	} else {
		modes = make_obj(wk, obj_dict);
		obj_dict_set(wk, fn, make_str(wk, "modes"), modes);
		obj_array_push(wk, doc, fn);
		hash_set_strn(wk->a_scratch, wk->a_scratch, map, key.buf, key.len, fn);
	}

	const char *mode_str = "";
	switch (wk->vm.lang_mode) {
	case language_external:
		mode_str = "external"; break;
	case language_internal:
		mode_str = "internal"; break;
	case language_opts:
		mode_str = "opts"; break;
	default: UNREACHABLE;
	}

	obj_dict_set(wk, modes, make_str(wk, mode_str), obj_bool_true);
}

static void
dump_function_docs_for_mode(struct workspace *wk, obj doc, struct hash *map)
{
	struct func_impl_group *g;

	uint32_t i;
	{
		enum obj_type t;
		for (t = 0; t < obj_type_count; ++t) {
			g = &func_impl_groups[t][wk->vm.lang_mode];
			if (!g->impls) {
				continue;
			}

			for (i = 0; i < g->len; ++i) {
				workspace_scratch_begin(wk);
				obj fn = dump_function_native(wk, t, &g->impls[i]);
				workspace_scratch_end(wk);
				dump_function_docs_push(wk, doc, fn, map);
			}
		}
	}

	for (i = 0; i < module_count; ++i) {
		g = &module_func_impl_groups[i][wk->vm.lang_mode];
		if (!g->impls) {
			continue;
		}

		uint32_t j;
		for (j = 0; j < g->len; ++j) {
			workspace_scratch_begin(wk);
			obj fn = dump_module_function_native(wk, i, &g->impls[j]);
			workspace_scratch_end(wk);
			dump_function_docs_push(wk, doc, fn, map);
		}
	}

	// get docs for script modules.
	if (wk->vm.lang_mode == language_external) {
		uint32_t i, embedded_len;
		const struct embedded_file *files = embedded_file_list(&embedded_len);
		const struct str *prefix = &STR("modules/"), *str;

		for (i = 0; i < embedded_len; ++i) {
			str = &STRL(files[i].name);
			if (!str_startswith(str, prefix)) {
				continue;
			}

			TSTR(mod_name);
			tstr_pushs(wk, &mod_name, files[i].name + prefix->len);
			*strchr(mod_name.buf, '.') = 0;

			struct obj_module *m;
			{
				obj mod;
				if (!module_import(wk, mod_name.buf, true, &mod)) {
					UNREACHABLE;
				}

				m = get_obj_module(wk, mod);
				assert(m->found);
			}

			TSTR(mod_path);
			tstr_pushf(wk, &mod_path, "public/%s", mod_name.buf);

			obj k, v;
			obj_dict_for(wk, m->exports, k, v) {
				workspace_scratch_begin(wk);
				obj fn = dump_module_function_capture(wk, mod_path.buf, k, v);
				workspace_scratch_end(wk);
				dump_function_docs_push(wk, doc, fn, map);
			}
		}
	}
}

static int32_t optional_field_cmp(const struct str *a, const struct str *b)
{
	if (a && b) {
		return strcmp(a->s, b->s);
	} else if (a) {
		return 1;
	} else if (b) {
		return -1;
	} else {
		return 0;
	}
}

static int32_t
dump_function_docs_sort_func(struct workspace *wk, void *_ctx, obj fn_a, obj fn_b)
{
	struct {
		const struct str *name, *rcvr, *module;
	} a = { 0 }, b = { 0 };

	a.name = obj_dict_index_as_str(wk, fn_a, "name");
	a.rcvr = obj_dict_index_as_str(wk, fn_a, "rcvr");
	a.module = obj_dict_index_as_str(wk, fn_a, "module");

	b.name = obj_dict_index_as_str(wk, fn_b, "name");
	b.rcvr = obj_dict_index_as_str(wk, fn_b, "rcvr");
	b.module = obj_dict_index_as_str(wk, fn_b, "module");

	int32_t cmp;
	if ((cmp = optional_field_cmp(a.module, b.module)) == 0) {
		if ((cmp = optional_field_cmp(a.rcvr, b.rcvr)) == 0) {
			return strcmp(a.name->s, b.name->s);
		} else {
			return cmp;
		}
	} else {
		return cmp;
	}
}

static obj
dump_function_docs_obj(struct workspace *wk)
{
	stack_push(&wk->stack, wk->vm.dumping_docs, true);

	workspace_scratch_begin(wk);

	struct hash map;
	hash_init_str(wk->a_scratch, &map, 1024);

	obj docs = make_obj(wk, obj_array);

	stack_push(&wk->stack, wk->vm.lang_mode, language_external);
	dump_function_docs_for_mode(wk, docs, &map);

	wk->vm.lang_mode = language_internal;
	dump_function_docs_for_mode(wk, docs, &map);

	wk->vm.lang_mode = language_opts;
	dump_function_docs_for_mode(wk, docs, &map);
	stack_pop(&wk->stack, wk->vm.lang_mode);

	obj sorted;
	obj_array_sort(wk, 0, docs, dump_function_docs_sort_func, &sorted);

	workspace_scratch_end(wk);

	stack_pop(&wk->stack, wk->vm.dumping_docs);

	return sorted;
}

/*******************************************************************************
 * json/html generators
 ******************************************************************************/

static void
dump_function_docs_json(struct workspace *wk, struct tstr *buf)
{
	obj root = make_obj(wk, obj_dict);

	obj_dict_set(wk, root, make_str(wk, "functions"), dump_function_docs_obj(wk));

	obj version = make_obj(wk, obj_dict);
	obj_dict_set(wk, version, make_str(wk, "version"), make_str(wk, muon_version.version));
	obj_dict_set(wk, version, make_str(wk, "vcs_tag"), make_str(wk, muon_version.vcs_tag));
	obj_dict_set(wk, version, make_str(wk, "meson_compat"), make_str(wk, muon_version.meson_compat));
	obj_dict_set(wk, root, make_str(wk, "version"), version);

	if (!obj_to_json(wk, root, buf)) {
		UNREACHABLE;
	}
}

static void
dump_function_docs_html(struct workspace *wk, struct tstr *buf)
{
	TSTR(docs_json);
	dump_function_docs_json(wk, &docs_json);

	struct source src;
	if (!embedded_get(wk, "html/docs.html", &src)) {
		UNREACHABLE;
	}

	tstr_pushf(wk, buf, src.src, docs_json.buf);
}

/*******************************************************************************
 * man generator
 ******************************************************************************/

static void
mw_reset_font(struct workspace *wk, struct tstr *buf)
{
	tstr_pushs(wk, buf, ".P\n");
}

static void
mw_title(struct workspace *wk, struct tstr *buf, const char *name, uint32_t section, const char *date)
{
	mw_reset_font(wk, buf);
	tstr_pushf(wk, buf, ".TH \"%s\" \"%d\" \"%s\"\n", name, section, date);
}

static void
MUON_ATTR_FORMAT(printf, 3, 4) mw_section(struct workspace *wk, struct tstr *buf, const char *fmt, ...)
{
	mw_reset_font(wk, buf);

	tstr_pushs(wk, buf, ".SH ");
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(wk, buf, fmt, args);
	va_end(args);
	tstr_push(wk, buf, '\n');
}

static void
MUON_ATTR_FORMAT(printf, 3, 4) mw_subsection(struct workspace *wk, struct tstr *buf, const char *fmt, ...)
{
	mw_reset_font(wk, buf);

	tstr_pushs(wk, buf, ".SS ");
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(wk, buf, fmt, args);
	va_end(args);
	tstr_push(wk, buf, '\n');
}

static void
MUON_ATTR_FORMAT(printf, 3, 4) mw_paragraph(struct workspace *wk, struct tstr *buf, const char *fmt, ...)
{
	mw_reset_font(wk, buf);
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(wk, buf, fmt, args);
	va_end(args);
	tstr_push(wk, buf, '\n');
}

static void
MUON_ATTR_FORMAT(printf, 3, 4) mw_bold(struct workspace *wk, struct tstr *buf, const char *fmt, ...)
{
	tstr_pushs(wk, buf, "\\fB");
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(wk, buf, fmt, args);
	va_end(args);
	tstr_pushs(wk, buf, "\\fR");
}

static void
MUON_ATTR_FORMAT(printf, 3, 4) mw_italic(struct workspace *wk, struct tstr *buf, const char *fmt, ...)
{
	tstr_pushs(wk, buf, "\\fI");
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(wk, buf, fmt, args);
	va_end(args);
	tstr_pushs(wk, buf, "\\fR");
}

static void
mw_indent(struct workspace *wk, struct tstr *buf, uint32_t amount)
{
	tstr_pushf(wk, buf, ".RS %d\n", amount * 4);
}

static void
mw_unindent(struct workspace *wk, struct tstr *buf)
{
	tstr_pushf(wk, buf, ".RE\n");
}

static void
mw_br(struct workspace *wk, struct tstr *buf)
{
	tstr_pushf(wk, buf, ".br\n");
}

static void
mw_nl(struct workspace *wk, struct tstr *buf)
{
	tstr_pushf(wk, buf, "\n");
}

static void
MUON_ATTR_FORMAT(printf, 3, 4) mw_line(struct workspace *wk, struct tstr *buf, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(wk, buf, fmt, args);
	va_end(args);
	tstr_push(wk, buf, '\n');
}

static void
MUON_ATTR_FORMAT(printf, 3, 4) mw_raw(struct workspace *wk, struct tstr *buf, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(wk, buf, fmt, args);
	va_end(args);
}

static void
mw_description(struct workspace *wk, struct tstr *buf, const char *desc)
{
	mw_line(wk, buf, "%s", desc);
}

static void
mw_subsubsection(struct workspace *wk, struct tstr *buf, const char *title, const char *desc)
{
	mw_bold(wk, buf, "%s", title);
	mw_nl(wk, buf);
	mw_indent(wk, buf, 1);
	mw_description(wk, buf, desc);
	mw_unindent(wk, buf);
}

static void
mw_function_args(struct workspace *wk, struct tstr *buf, const char *title, obj args)
{
	if (!args) {
		return;
	}

	mw_bold(wk, buf, "%s", title);
	mw_nl(wk, buf);
	mw_indent(wk, buf, 1);
	obj v;
	obj_array_for(wk, args, v) {
		mw_bold(wk, buf, "%s", obj_dict_index_as_str(wk, v, "name")->s);
		mw_raw(wk, buf, " ");
		mw_italic(wk, buf, "%s", obj_dict_index_as_str(wk, v, "type")->s);
		mw_nl(wk, buf);
		const struct str *desc;
		if ((desc = obj_dict_index_as_str(wk, v, "desc"))) {
			mw_br(wk, buf);
			mw_indent(wk, buf, 1);
			mw_description(wk, buf, desc->s);
			mw_unindent(wk, buf);
			mw_nl(wk, buf);
		}
	}
	mw_unindent(wk, buf);
}

static void
mw_function(struct workspace *wk, struct tstr *buf, obj fn)
{
	obj posargs, kwargs, v;
	const char *fname = obj_dict_index_as_str(wk, fn, "name")->s;
	{
		const struct str *rcvr = obj_dict_index_as_str(wk, fn, "rcvr");
		if (!rcvr) {
			rcvr = obj_dict_index_as_str(wk, fn, "module");
		}

		mw_subsection(wk, buf, "%s%s%s()", rcvr ? rcvr->s : "", rcvr ? "." : "", fname);
	}
	mw_indent(wk, buf, 0);

	mw_bold(wk, buf, "SYNOPSIS");
	mw_nl(wk, buf);

	posargs = obj_dict_index_as_obj(wk, fn, "posargs");
	kwargs = obj_dict_index_as_obj(wk, fn, "kwargs");

	{ // signature
		mw_line(wk, buf, "%s(", fname);
		mw_br(wk, buf);
		mw_indent(wk, buf, 1);

		if (posargs) {
			obj_array_for(wk, posargs, v) {
				mw_italic(wk, buf, "%s", obj_dict_index_as_str(wk, v, "type")->s);
				mw_raw(wk, buf, ",\n");
				mw_br(wk, buf);
			}
		}

		if (kwargs) {
			obj_array_for(wk, kwargs, v) {
				mw_raw(wk, buf, "%s ", obj_dict_index_as_str(wk, v, "name")->s);
				mw_italic(wk, buf, "%s", obj_dict_index_as_str(wk, v, "type")->s);
				mw_raw(wk, buf, ":,\n");
				mw_br(wk, buf);
			}
		}

		mw_unindent(wk, buf);
		mw_raw(wk, buf, ") -> ");
		mw_italic(wk, buf, "%s", obj_dict_index_as_str(wk, fn, "type")->s);
		mw_nl(wk, buf);
	}

	mw_nl(wk, buf);

	const struct str *desc;
	if ((desc = obj_dict_index_as_str(wk, fn, "desc"))) {
		mw_subsubsection(wk, buf, "DESCRIPTION", desc->s);
	}
	mw_nl(wk, buf);

	mw_function_args(wk, buf, "POSARGS", posargs);
	mw_function_args(wk, buf, "KWARGS", kwargs);

	mw_unindent(wk, buf);
}

static void
dump_function_docs_man(struct workspace *wk, struct tstr *buf)
{
	obj fn, functions = dump_function_docs_obj(wk);

	char version_buf[256];
	snprintf(version_buf, sizeof(version_buf), "muon %s%s%s", muon_version.version, muon_version.vcs_tag ? "-" : "", muon_version.vcs_tag);
	mw_title(wk, buf, "meson-reference", 3, version_buf);

	mw_section(wk, buf, "NAME");
	mw_paragraph(wk, buf, "meson-reference %s - a reference for meson functions and objects", muon_version.meson_compat);

	mw_section(wk, buf, "DESCRIPTION");
	mw_paragraph(wk,
		buf,
		"This manual is divided into three sections, *KERNEL FUNCTIONS*, *OBJECT METHODS*, and *MODULE FUNCTIONS*. "
		"*KERNEL FUNCTIONS* contains all builtin meson functions and methods. "
		"Methods and module functions are denoted by [[object / module name]].[[method_name]]().");

	obj prev_rcvr = 0, prev_module = 0, rcvr, module;
	mw_section(wk, buf, "KERNEL FUNCTIONS");
	obj_array_for(wk, functions, fn) {
		rcvr = obj_dict_index_as_obj(wk, fn, "rcvr");
		module = obj_dict_index_as_obj(wk, fn, "module");

		if (!obj_equal(wk, rcvr, prev_rcvr) || !obj_equal(wk, module, prev_module)) {
			if (!prev_rcvr && rcvr) {
				mw_section(wk, buf, "OBJECT METHODS");
			}
			if (rcvr) {
				mw_section(wk, buf, "%s", get_str(wk, rcvr)->s);
			}

			if (!prev_module && module) {
				mw_section(wk, buf, "MODULE FUNCTIONS");
			}
			if (module) {
				mw_section(wk, buf, "%s", get_str(wk, module)->s);
			}
		}

		mw_function(wk, buf, fn);

		prev_rcvr = rcvr;
		prev_module = module;
	}

	// mw_section(wk, buf, "SEE ALSO");

	mw_section(wk, buf, "COPYRIGHT");
	mw_paragraph(wk,
		buf,
		"Documentation comes from muon (https://muon.build/) and the meson project (https://mesonbuild.com) "
		"and is released under Attribution-ShareAlike 4.0 International (CC BY-SA 4.0). "
		"Code samples are released under CC0 1.0 Universal (CC0 1.0).");
	mw_paragraph(wk, buf, "Meson is a registered trademark of Jussi Pakkanen.");

	mw_paragraph(wk, buf, "Generated with %s", version_buf);
}

/*******************************************************************************
 * entrypoint
 ******************************************************************************/

void
dump_function_docs(struct workspace *wk, const struct dump_function_docs_opts* opts)
{
	TSTR(buf);

	switch (opts->type) {
	case dump_function_docs_output_html: dump_function_docs_html(wk, &buf); break;
	case dump_function_docs_output_json: dump_function_docs_json(wk, &buf); break;
	case dump_function_docs_output_man: dump_function_docs_man(wk, &buf); break;
	}

	fprintf(opts->out, "%s", buf.buf);
}

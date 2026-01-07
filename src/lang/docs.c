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
#include "opts.h"
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
		"%s%s%s%s",
		rcvr ? get_str(wk, rcvr)->s : "",
		module ? get_str(wk, module)->s : "",
		rcvr || module ? "." : "",
		get_str(wk, name)->s);

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

enum man_text_style {
	man_text_style_regular,
	man_text_style_bold,
	man_text_style_italic,
};

struct man_writer {
	struct workspace *wk;
	struct tstr *buf;
	enum man_text_style text_style;
};

static void
mw_raw(struct man_writer *mw, const char *s)
{
	tstr_pushs(mw->wk, mw->buf, s);
}

static void
mw_rawc(struct man_writer *mw, char s)
{
	tstr_push(mw->wk, mw->buf, s);
}

static void
MUON_ATTR_FORMAT(printf, 2, 3) mw_rawf(struct man_writer *mw, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(mw->wk, mw->buf, fmt, args);
	va_end(args);
}

static void
mw_paragraph(struct man_writer *mw)
{
	mw_raw(mw, ".PP\n");
}

static void
mw_title(struct man_writer *mw, const char *name, uint32_t section)
{
	char version_buf[256];
	snprintf(version_buf,
		sizeof(version_buf),
		"muon %s%s%s",
		muon_version.version,
		muon_version.vcs_tag ? "-" : "",
		muon_version.vcs_tag);
	mw_rawf(mw, ".TH \"%s\" \"%d\" \"%s\"\n", name, section, version_buf);
}

static void
MUON_ATTR_FORMAT(printf, 2, 3) mw_section(struct man_writer *mw, const char *fmt, ...)
{
	mw_rawf(mw, ".SH ");
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(mw->wk, mw->buf, fmt, args);
	va_end(args);
	mw_raw(mw, "\n");
}

static void
MUON_ATTR_FORMAT(printf, 2, 3) mw_subsection(struct man_writer *mw, const char *fmt, ...)
{
	mw_rawf(mw, ".SS ");
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(mw->wk, mw->buf, fmt, args);
	va_end(args);
	mw_raw(mw, "\n");
}

static void
MUON_ATTR_FORMAT(printf, 2, 3) mw_paragraph_text(struct man_writer *mw, const char *fmt, ...)
{
	mw_paragraph(mw);
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(mw->wk, mw->buf, fmt, args);
	va_end(args);
	mw_raw(mw, "\n");
}

static void
mw_paragraph_linesep(struct man_writer *mw, int32_t n)
{
	if (n == -1){
		mw_raw(mw, ".PD\n");
	} else {
		mw_rawf(mw, ".PD %d\n", n);
	}
}

static void
mw_fmt_bold(struct man_writer *mw)
{
	mw_raw(mw, "\\fB");
	mw->text_style = man_text_style_bold;
}

static void
mw_fmt_italic(struct man_writer *mw)
{
	mw_raw(mw, "\\fI");
	mw->text_style = man_text_style_italic;
}

static void
mw_fmt_regular(struct man_writer *mw)
{
	mw_raw(mw, "\\fR");
	mw->text_style = man_text_style_regular;
}

static void
MUON_ATTR_FORMAT(printf, 2, 3) mw_bold(struct man_writer *mw, const char *fmt, ...)
{
	mw_fmt_bold(mw);
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(mw->wk, mw->buf, fmt, args);
	va_end(args);
	mw_fmt_regular(mw);
}

static void
MUON_ATTR_FORMAT(printf, 2, 3) mw_italic(struct man_writer *mw, const char *fmt, ...)
{
	mw_fmt_italic(mw);
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(mw->wk, mw->buf, fmt, args);
	va_end(args);
	mw_fmt_regular(mw);
}

static void
mw_indent(struct man_writer *mw, uint32_t amount)
{
	mw_rawf(mw, ".RS %d\n", amount * 4);
}

static void
mw_unindent(struct man_writer *mw)
{
	mw_raw(mw, ".RE\n");
}

static void
mw_paragraph_fill(struct man_writer *mw)
{
	mw_raw(mw, ".fi\n");
}

static void
mw_paragraph_nofill(struct man_writer *mw)
{
	mw_raw(mw, ".nf\n");
}

static void
mw_nl(struct man_writer *mw)
{
	mw_raw(mw, "\n");
}

static void
mw_bullet(struct man_writer *mw)
{
	mw_raw(mw, ".IP \\(bu 4\n");
}

static void
mw_md(struct man_writer *mw, const char *text)
{
	bool in_code_block = false, in_inline_code = false, in_list = false;
	bool sol = true;

	char prev = 0;
	for (const char *p = text; *p; prev = *p, ++p) {
		bool ignore_format = (in_code_block || in_inline_code);

		switch (*p) {
		case '*':
		case '-':
			if (sol && p[1] == ' ') {
				++p;
				if (!in_list) {
					in_list = true;
					mw_paragraph_linesep(mw, 0);
				}
				mw_bullet(mw);
				sol = false;
				continue;
			} else if (*p == '*') {
				if (ignore_format) {
					break;
				}
				if (mw->text_style == man_text_style_bold) {
					mw_fmt_regular(mw);
				} else {
					mw_fmt_bold(mw);
				}
				continue;
			}
			break;
		case '[':
			if (ignore_format) {
				break;
			}
			if (p[1] == '[') {
				++p;
			}
			break;
		case ']':
			if (ignore_format) {
				break;
			}
			if (p[1] == ']') {
				++p;
			}
			mw_raw(mw, "]");
			if (p[1] == '(') {
				for (; *p != ')'; ++p) {
				}
			}
			continue;
		case '<':
			if (ignore_format) {
				break;
			}
			mw_rawc(mw, *p);
			mw_fmt_italic(mw);
			continue;
		case '>':
			if (ignore_format) {
				break;
			}
			mw_fmt_regular(mw);
			mw_rawc(mw, *p);
			continue;
		case '_':
			if (ignore_format) {
				break;
			}
			if (mw->text_style == man_text_style_italic && p[1] == ' ') {
				mw_fmt_regular(mw);
			} else if (prev == ' ') {
				mw_fmt_italic(mw);
			}
			continue;
		case '.':
			mw_raw(mw, ".\\&");
			continue;
		case '`':
			if (p[1] == '`' && p[2] == '`') {
				for (; *p && *p != '\n'; ++p) {
				}
				if (in_code_block) {
					mw_nl(mw);
					mw_paragraph_fill(mw);
					mw_unindent(mw);
					mw_paragraph(mw);
					in_code_block = false;
					sol = true;
				} else {
					mw_paragraph_nofill(mw);
					mw_indent(mw, 1);
					in_code_block = true;
				}
				continue;
			} else if (in_code_block) {
				break;
			} else if (in_inline_code) {
				in_inline_code = false;
			} else {
				in_inline_code = true;
			}
			continue;
		case '\n':
			if (in_code_block) {
				break;
			} else if (p[1] == '\n') {
				p += 1;
				mw_nl(mw);
				sol = true;
				if (in_list) {
					mw_paragraph_linesep(mw, -1);
					in_list = false;
				}
				mw_paragraph(mw);
			} else if (in_list) {
				if (p[1] == ' ' && p[2] == ' ') {
					p += 2;
					mw_rawc(mw, ' ');
				} else {
					mw_nl(mw);
					sol = true;
				}
			} else {
				if (!sol) {
					mw_rawc(mw, ' ');
				}
			}
			continue;
		}

		mw_rawc(mw, *p);
		if (sol) {
			sol = false;
			if (in_list) {
				mw_paragraph_linesep(mw, -1);
				in_list = false;
			}
		}
	}
}

static void
mw_br(struct man_writer *mw)
{
	mw_raw(mw, ".br\n");
}

static void
MUON_ATTR_FORMAT(printf, 2, 3) mw_line(struct man_writer *mw, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tstr_vpushf(mw->wk, mw->buf, fmt, args);
	va_end(args);
	mw_raw(mw, "\n");
}

static void
mw_description(struct man_writer *mw, const char *desc)
{
	mw_paragraph(mw);
	mw_md(mw, desc);
	mw_nl(mw);
}

static void
mw_subsubsection(struct man_writer *mw, const char *title, const char *desc)
{
	mw_bold(mw, "%s", title);
	mw_nl(mw);
	mw_indent(mw, 1);
	mw_description(mw, desc);
	mw_unindent(mw);
}

static void
mw_function_args(struct workspace *wk, struct man_writer *mw, const char *title, obj args)
{
	if (!args) {
		return;
	}

	mw_bold(mw, "%s", title);
	mw_nl(mw);
	mw_indent(mw, 1);
	obj v;
	obj_array_for(wk, args, v) {
		mw_bold(mw, "%s", obj_dict_index_as_str(wk, v, "name")->s);
		mw_raw(mw, " ");
		mw_italic(mw, "%s", obj_dict_index_as_str(wk, v, "type")->s);
		mw_nl(mw);
		const struct str *desc;
		if ((desc = obj_dict_index_as_str(wk, v, "desc"))) {
			mw_br(mw);
			mw_indent(mw, 1);
			mw_description(mw, desc->s);
			mw_unindent(mw);
			mw_nl(mw);
		}
	}
	mw_unindent(mw);
}

static void
mw_function(struct workspace *wk, struct man_writer *mw, obj fn)
{
	obj posargs, kwargs, v;
	const char *fname = obj_dict_index_as_str(wk, fn, "name")->s;
	{
		const struct str *rcvr = obj_dict_index_as_str(wk, fn, "rcvr");
		if (!rcvr) {
			rcvr = obj_dict_index_as_str(wk, fn, "module");
		}

		mw_subsection(mw, "%s%s%s()", rcvr ? rcvr->s : "", rcvr ? "." : "", fname);
	}
	mw_indent(mw, 0);

	mw_bold(mw, "SYNOPSIS");
	mw_nl(mw);
	mw_indent(mw, 1);

	posargs = obj_dict_index_as_obj(wk, fn, "posargs");
	kwargs = obj_dict_index_as_obj(wk, fn, "kwargs");

	{ // signature
		mw_line(mw, "%s(", fname);
		mw_br(mw);
		mw_indent(mw, 1);

		if (posargs) {
			obj_array_for(wk, posargs, v) {
				mw_italic(mw, "%s", obj_dict_index_as_str(wk, v, "type")->s);
				mw_raw(mw, ",\n");
				mw_br(mw);
			}
		}

		if (kwargs) {
			obj_array_for(wk, kwargs, v) {
				mw_rawf(mw, "%s ", obj_dict_index_as_str(wk, v, "name")->s);
				mw_italic(mw, "%s", obj_dict_index_as_str(wk, v, "type")->s);
				mw_raw(mw, ":,\n");
				mw_br(mw);
			}
		}

		mw_unindent(mw);
		mw_raw(mw, ")");
		const struct str *type = obj_dict_index_as_str(wk, fn, "type");
		if (!str_eql(type, &STR("null"))) {
			mw_raw(mw, " -> ");
			mw_italic(mw, "%s", type->s);
		}
		mw_nl(mw);
	}

	mw_nl(mw);
	mw_unindent(mw);

	const struct str *desc;
	if ((desc = obj_dict_index_as_str(wk, fn, "desc"))) {
		mw_subsubsection(mw, "DESCRIPTION", desc->s);
	}
	mw_nl(mw);

	mw_function_args(wk, mw, "POSARGS", posargs);
	mw_function_args(wk, mw, "KWARGS", kwargs);

	mw_unindent(mw);
}

static void
dump_function_docs_man(struct workspace *wk, struct man_writer *mw, const char *query)
{
	obj fn, functions = dump_function_docs_obj(wk);

	mw_title(mw, "meson-reference", 3);

	mw_section(mw, "NAME");
	if (query) {
		mw_paragraph_text(mw, "results for '%s'", query);
	} else {
		mw_paragraph_text(mw, "meson-reference %s - a reference for meson functions and objects", muon_version.meson_compat);
	}

	if (!query) {
		mw_section(mw, "DESCRIPTION");
		mw_description(mw,
			"This manual is divided into three sections, *KERNEL FUNCTIONS*, *OBJECT METHODS*, and *MODULE FUNCTIONS*. "
			"*KERNEL FUNCTIONS* contains all builtin meson functions and methods. "
			"Methods and module functions are denoted by [[object / module name]].[[method_name]]().");
	}

	obj prev_rcvr = 1, prev_module = 0, rcvr, module;
	obj_array_for(wk, functions, fn) {
		if (query) {
			TSTR(key);
			dump_function_docs_fn_key(wk, &key, fn);
			if (!strstr(key.buf, query)) {
				continue;
			}
		}

		rcvr = obj_dict_index_as_obj(wk, fn, "rcvr");
		module = obj_dict_index_as_obj(wk, fn, "module");

		if (!obj_equal(wk, rcvr, prev_rcvr) || !obj_equal(wk, module, prev_module)) {
			if (!rcvr && !module) {
				mw_section(mw, "KERNEL FUNCTIONS");
			}

			if (!prev_rcvr && rcvr) {
				mw_section(mw, "OBJECT METHODS");
			}
			if (rcvr) {
				mw_section(mw, "%s", get_str(wk, rcvr)->s);
			}

			if (!prev_module && module) {
				mw_section(mw, "MODULE FUNCTIONS");
			}
			if (module) {
				mw_section(mw, "%s", get_str(wk, module)->s);
			}
		}

		mw_function(wk, mw, fn);

		prev_rcvr = rcvr;
		prev_module = module;
	}

	// mw_section(mw, "SEE ALSO");

	if (!query) {
		mw_section(mw, "COPYRIGHT");
		mw_paragraph_text(mw,
			"Documentation comes from muon (https://muon.build/) and the meson project (https://mesonbuild.com) "
			"and is released under Attribution-ShareAlike 4.0 International (CC BY-SA 4.0). "
			"Code samples are released under CC0 1.0 Universal (CC0 1.0).");
		mw_paragraph_text(mw, "Meson is a registered trademark of Jussi Pakkanen.");
	}
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
	case dump_function_docs_output_man: {
		struct man_writer mw = { .wk = wk, .buf = &buf };
		dump_function_docs_man(wk, &mw, opts->query);
	} break;
	}

	fprintf(opts->out, "%s", buf.buf);
}

static void
mw_cli_option_value(struct man_writer *mw, const struct opt_match_opts *opt)
{
	if (opt->enum_table_len) {
		mw_raw(mw, " <");
		mw_italic(mw, "%s", opt->enum_table[0].long_name);
		for (uint32_t i = 1; i < opt->enum_table_len; ++i) {
			mw_raw(mw, "|");
			mw_italic(mw, "%s", opt->enum_table[i].long_name);
		}
		mw_raw(mw, ">");
	} else if (opt->value_name) {
		mw_raw(mw, " <");
		mw_fmt_italic(mw);
		mw_md(mw, opt->value_name);
		mw_fmt_regular(mw);
		mw_raw(mw, ">");
	}
}

static void
mw_cli_options(struct man_writer *mw, const struct arr *opts)
{
	mw_paragraph_linesep(mw, 0);
	for (uint32_t i = 0; i < opts->len; ++i) {
		const struct opt_match_opts *opt = arr_get(opts, i);
		mw_bullet(mw);
		mw_bold(mw, "-%c", opt->c);
		mw_cli_option_value(mw, opt);
		mw_raw(mw, " - ");
		mw_md(mw, opt->desc);
		if (opt->desc_long) {
			mw_raw(mw, " ");
			mw_md(mw, opt->desc_long);
		}
		mw_nl(mw);
	}
	mw_paragraph_linesep(mw, -1);
}

static void
mw_synopsis(struct man_writer *mw, const struct opt_gathered_command *cmd, const char *cmd_name)
{
	const struct arr *opts = &cmd->opts;
	mw_bold(mw, "muon %s", cmd_name);
	if (opts->len) {
		bool any_opt_requires_no_value = false;

		for (uint32_t i = 0; i < opts->len; ++i) {
			const struct opt_match_opts *opt = arr_get(opts, i);
			if (opt->c == 'h') {
				continue;
			}
			if (!opt->enum_table_len && !opt->value_name) {
				any_opt_requires_no_value = true;
				break;
			}
		}

		if (any_opt_requires_no_value) {
			mw_raw(mw, " [");
			mw_bold(mw, "-");
			for (uint32_t i = 0; i < opts->len; ++i) {
				const struct opt_match_opts *opt = arr_get(opts, i);
				if (opt->c == 'h') {
					continue;
				}
				if (!opt->enum_table_len && !opt->value_name) {
					mw_bold(mw, "%c", opt->c);
				}
			}
			mw_raw(mw, "]");
		}

		for (uint32_t i = 0; i < opts->len; ++i) {
			const struct opt_match_opts *opt = arr_get(opts, i);
			if (!opt->enum_table_len && !opt->value_name) {
				continue;
			}

			mw_raw(mw, " [");
			mw_bold(mw, "-%c", opt->c);
			mw_cli_option_value(mw, opt);
			mw_raw(mw, "]");
		}
	}

	if (cmd->usage_post) {
		mw_md(mw, cmd->usage_post);
	}

	if (cmd->commands) {
		mw_md(mw, " <command>");
	}
}

static void
dump_cli_docs_man(struct workspace *wk, struct man_writer *mw, const char *query, const struct arr *commands)
{
	mw_title(mw, "muon", 1);

	mw_section(mw, "NAME");
	if (query) {
		mw_paragraph_text(mw, "results for '%s'", query);
	} else {
		mw_paragraph_text(mw, "muon - a meson-compatible build system");
	}

	if (!query) {
		mw_section(mw, "SYNOPSIS");

		mw_description(mw, "*muon* [*-vh*] [*-C* <chdir>] <command> [<args>]");
		mw_br(mw);
		mw_nl(mw);
		mw_md(mw, "*muon* *build* [*-D*[subproject*:*]option*=*value...] <build dir>");
		mw_nl(mw);
		mw_br(mw);
		mw_md(mw, "*muon* -C <build dir> *test* [options]");
		mw_nl(mw);
		mw_br(mw);
		mw_md(mw, "*muon* -C <build dir> *install* [options]");
		mw_nl(mw);
		mw_br(mw);

		mw_section(mw, "DESCRIPTION");

		mw_description(mw,
			"*muon* interprets _source files_ written in the _meson dsl_ and produces "
			"_buildfiles_ for a backend. "
			"\n\n"
			"When building *meson* projects with *muon*, you typically first start by "
			"running the *setup* command in the project root.  This will create _buildfiles_ "
			"for the backend in the _build dir_ you specify.  You then invoke the backend, "
			"e.g. "
			"\n\n"
			"```\n"
			"ninja -C <build dir>\n"
			"```"
			"\n\n"
			"If the project defines tests, you may run them with the *test* subcommand, and "
			"finally install the project with the *install* subcommand.");

		mw_section(mw, "OPTIONS");
		const struct opt_gathered_command *root_cmd = arr_get(commands, 0);
		mw_cli_options(mw, &root_cmd->opts);
	}

	mw_section(mw, "COMMANDS");

	if (!query) {
		mw_paragraph(mw);
		mw_description(mw, "*muon* requires a command");

		mw_paragraph(mw);
		mw_description(mw, "All commands accept a *-h* option which prints a brief summary of their usage.");
	}

	for (uint32_t i = 1; i < commands->len; ++i) {
		const struct opt_gathered_command *cmd = arr_get(commands, i);

		obj joined;
		obj_array_join(wk, false, cmd->trace, make_str(wk, " "), &joined);
		const char *cmd_name = get_str(wk, joined)->s;
		if (query && !strstr(cmd_name, query)) {
			continue;
		}
		mw_subsection(mw, "%s", cmd_name);
		mw_indent(mw, 1);

		mw_synopsis(mw, cmd, cmd_name);
		mw_nl(mw);
		mw_br(mw);

		mw_description(mw, cmd->desc);
		mw_br(mw);

		if (cmd->desc_long) {
			mw_description(mw, cmd->desc_long);
			mw_br(mw);
		}

		if (cmd->opts.len) {
			mw_paragraph(mw);
			mw_bold(mw, "OPTIONS");
			mw_nl(mw);
			mw_cli_options(mw, &cmd->opts);
		}

		if (cmd->commands) {
			mw_paragraph(mw);
			mw_bold(mw, "COMMANDS");
			mw_nl(mw);
			mw_paragraph_linesep(mw, 0);
			for (uint32_t i = 0; cmd->commands[i].name; ++i) {
				const struct opt_command *sub = &cmd->commands[i];
				if (sub->desc && !sub->skip_gather) {
					mw_bullet(mw);
					mw_bold(mw, "%s", sub->name);
					mw_raw(mw, " - ");
					mw_md(mw, sub->desc);
					mw_nl(mw);
				}
			}
			mw_paragraph_linesep(mw, -1);
		}
	}

	if (!query) {
		mw_section(mw, "SEE ALSO");
		mw_description(mw, "meson.build(5) meson-reference(3) meson(1)");

		mw_section(mw, "AUTHORS");
		mw_description(mw,
			"Maintained by Stone Tickle <lattis@mochiro.moe>, who is assisted by other open "
			"source contributors.  For more information about muon development, see "
			"<https://sr.ht/~lattis/muon>.");
	}
}

void
dump_cli_docs(struct workspace *wk, const struct dump_function_docs_opts* opts, const struct arr *commands)
{
	TSTR(buf);

	switch (opts->type) {
	case dump_function_docs_output_html: UNREACHABLE; break;
	case dump_function_docs_output_json: UNREACHABLE; break;
	case dump_function_docs_output_man: {
		struct man_writer mw = { .wk = wk, .buf = &buf };
		dump_cli_docs_man(wk, &mw, opts->query, commands);
	} break;
	}

	fprintf(opts->out, "%s", buf.buf);
}

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_DOCS_H
#define MUON_LANG_DOCS_H

#include "lang/object.h"

struct func_impl;


enum dump_function_docs_output {
	dump_function_docs_output_html,
	dump_function_docs_output_json,
	dump_function_docs_output_man,
};

struct dump_function_docs_opts {
	enum dump_function_docs_output type;
	FILE* out;
};

void dump_function_docs(struct workspace *wk, const struct dump_function_docs_opts* opts);
obj dump_function_native(struct workspace *wk, enum obj_type t, const struct func_impl *impl);
obj dump_module_function_native(struct workspace *wk, enum module module, const struct func_impl *impl);
obj dump_module_function_capture(struct workspace *wk, const char *module, obj name, obj o);
#endif

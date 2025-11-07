/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_FMT_H
#define MUON_LANG_FMT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum fmt_indent_style {
	fmt_indent_style_space,
	fmt_indent_style_tab,
};

enum fmt_end_of_line {
	fmt_end_of_line_lf,
	fmt_end_of_line_crlf,
	fmt_end_of_line_cr,
};

struct fmt_opts {
	bool space_array, kwargs_force_multiline, wide_colon, no_single_comma_function, insert_final_newline,
		sort_files, group_arg_value, simplify_string_literals, sticky_parens, continuation_indent;
	uint32_t max_line_len;

	uint32_t indent_style; // enum fmt_indent_style
	uint32_t indent_size;
	uint32_t tab_width;

	uint32_t end_of_line; // enum fmt_end_of_line

	const char *indent_before_comments;
	bool use_editor_config; // ignored for now
};

struct arena;
struct source;
bool
fmt(struct arena *a,
	struct arena *a_scratch,
	struct source *src,
	FILE *out,
	const char *cfg_path,
	bool check_only,
	bool editorconfig);
#endif

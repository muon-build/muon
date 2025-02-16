/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "formats/editorconfig.h"
#include "formats/ini.h"
#include "lang/fmt.h"
#include "lang/string.h"
#include "platform/assert.h"
#include "platform/mem.h"
#include "platform/path.h"

struct parse_editorconfig_ctx {
	const char *path;
	bool was_root, matched;
	const char *indent_style, *indent_size, *tab_width, *max_line_length, *end_of_line, *insert_final_newline;
};

struct editorconfig_pat {
	char type;
	const char *pat, *end;
	uint32_t len;
	bool glob_slash;
};

/*
 * pattern types:
 * 'a' -> match literal character
 * '*' -> glob match, * and ** are both handled by this type
 * '[' -> [name]
 * '!' -> [!name]
 * '{' -> {a,b,c}
 * '0' -> {num1..num2}
 * '?' -> ?
 */

static const char *
editorconfig_parse_pat(struct editorconfig_pat *pat, const char *c)
{
	if (!*c) {
		return c;
	} else if (*c == '\\') {
		++c;
		pat->type = 'a';
		pat->pat = c;
		pat->len = 1;
		++c;
	} else if (*c == '*') {
		pat->type = '*';
		pat->glob_slash = false;
		++c;
		if (*c == '*') {
			pat->glob_slash = true;
			++c;
		}
	} else if (*c == '[') {
		pat->type = '[';
		++c;
		if (*c == '!') {
			pat->type = '!';
			++c;
		}

		pat->pat = c;

		if ((pat->end = strchr(pat->pat, ']'))) {
			pat->len = pat->end - pat->pat - 1;
		} else {
			pat->len = strlen(pat->pat);
		}

		c += pat->len + 1;
	} else if (*c == '{') {
		++c;
		pat->pat = c;

		if (strstr(pat->pat, "..")) {
			pat->type = '0';
		} else {
			pat->type = '{';
		}

		if ((pat->end = strchr(pat->pat, '}'))) {
			pat->len = pat->end - pat->pat;
		} else {
			pat->len = strlen(pat->pat);
		}

		c += pat->len + 1;
	} else if (*c == '?') {
		pat->type = '?';
		++c;
	} else {
		pat->type = 'a';
		pat->pat = c;
		pat->len = 1;
		++c;
	}

	return c;
}

static bool
strnchr(const char *s, char c, uint32_t len)
{
	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (*s == c) {
			return true;
		}
	}

	return false;
}

/* #define L_editorconfig(...) for (uint32_t __i = 0; __i < depth; ++__i) { LL("  "); } L(__VA_ARGS__) */
#define L_editorconfig(...)

static const char *
editorconfig_pat_match(struct editorconfig_pat *pat,
	const char *path,
	const char *c,
	bool *consume_pattern,
	uint32_t depth)
{
	if (!pat->type) {
		return 0;
	}

	*consume_pattern = true;
	L_editorconfig(
		"MATCHING: type: %c, pat: '%.*s', glob_slash: %d", pat->type, pat->len, pat->pat, pat->glob_slash);
	L_editorconfig("remaining pattern: '%s', remaining path: '%s'", c, path);

	switch (pat->type) {
	case 'a': {
		if (*path == *pat->pat) {
			return path + 1;
		}
		break;
	}
	case '?': {
		if (*path) {
			return path + 1;
		}
		break;
	}
	case '[': {
		if (strnchr(pat->pat, *path, pat->len)) {
			return path + 1;
		}
		break;
	}
	case '!': {
		if (!strnchr(pat->pat, *path, pat->len)) {
			return path + 1;
		}
		break;
	}
	case '{': {
		const char *part, *end;
		uint32_t len;
		part = pat->pat;
		while (true) {
			if ((end = strchr(part, ','))) {
				len = end - part;
			} else {
				len = pat->len - (part - pat->pat);
			}

			L_editorconfig("trying part: %.*s", len, part);
			if (strncmp(path, part, len) == 0) {
				return path + len;
			}

			if (!end) {
				break;
			}

			part = end + 1;
		}
		break;
	}
	case '*': {
		const char *new_path;
		{
			struct editorconfig_pat next = { 0 };
			c = editorconfig_parse_pat(&next, c);
			bool sub_consume_pattern;
			const char *sub_path = path;

			while (true) {
				new_path = editorconfig_pat_match(&next, sub_path, c, &sub_consume_pattern, depth + 1);
				if (!new_path || !*new_path) {
					break;
				}

				sub_path = new_path;
				if (sub_consume_pattern) {
					c = editorconfig_parse_pat(&next, c);
				}
			}
		}

		if (new_path && !*new_path) {
			*consume_pattern = true;
			return path;
		} else if (*path == '/' && !pat->glob_slash) {
			return NULL;
		} else {
			*consume_pattern = false;
			return path + 1;
		}
		break;
	}
	case '0':
		// meson filenames can't have numbers in them so patterns that
		// have this default to no-match
		return NULL;
	default: UNREACHABLE;
	}

	return NULL;
}

bool
editorconfig_pattern_match(const char *pattern, const char *string)
{
	struct editorconfig_pat cur = { .glob_slash = true };
	const char *new_string;
	bool match = false;

	pattern = editorconfig_parse_pat(&cur, pattern);

	while (true) {
		bool consume_pattern;
		if (!(new_string = editorconfig_pat_match(&cur, string, pattern, &consume_pattern, 0))) {
			match = false;
		} else {
			if (consume_pattern) {
				pattern = editorconfig_parse_pat(&cur, pattern);
			}
			string = new_string;
			match = true;
		}

		if (!match || !*string) {
			break;
		}
	}

	if (*string || *pattern) {
		match = false;
	}

	return match;
}

static bool
editorconfig_cfg_parse_cb(void *_ctx,
	struct source *src,
	const char *sect,
	const char *k,
	const char *v,
	struct source_location location)
{
	struct parse_editorconfig_ctx *ctx = _ctx;

	if (!k) {
		return true;
	}

	struct str kstr = STRL(k);
	str_to_lower(&kstr);

	if (v) {
		struct str vstr = STRL(k);
		str_to_lower(&vstr);
	}

	if (!sect) {
		if (strcmp(k, "root") == 0 && strcmp(v, "true") == 0) {
			ctx->was_root = true;
		}

		return true;
	}

	struct editorconfig_pat cur = { .type = '*', .glob_slash = true };
	const char *c, *path = ctx->path, *new_path;
	bool match = true;

	c = sect;
	while (true) {
		bool consume_pattern;
		if (!(new_path = editorconfig_pat_match(&cur, path, c, &consume_pattern, 0))) {
			match = false;
		} else {
			if (consume_pattern) {
				c = editorconfig_parse_pat(&cur, c);
			}
			path = new_path;
			match = true;
		}

		if (!match || !*path) {
			break;
		}
	}

	if (*c || !match) {
		return true;
	}

	ctx->matched = true;

	if (strcmp(k, "indent_style") == 0) {
		ctx->indent_style = v;
	} else if (strcmp(k, "indent_size") == 0) {
		ctx->indent_size = v;
	} else if (strcmp(k, "tab_width") == 0) {
		ctx->tab_width = v;
	} else if (strcmp(k, "max_line_length") == 0) {
		ctx->max_line_length = v;
	} else if (strcmp(k, "end_of_line") == 0) {
		ctx->end_of_line = v;
	} else if (strcmp(k, "insert_final_newline") == 0) {
		ctx->insert_final_newline = v;
	}

	return true;
}

void
try_parse_editorconfig(struct source *src, struct fmt_opts *opts)
{
	TSTR_manual(path_abs);
	TSTR_manual(path);
	TSTR_manual(wd);
	path_make_absolute(0, &path_abs, src->label);
	path_copy(0, &path, path_abs.buf);
	path_dirname(0, &wd, path.buf);

	const char *indent_style = 0, *indent_size = 0, *tab_width = 0, *max_line_length = 0, *end_of_line = 0,
		   *insert_final_newline = 0;
	struct source cfg_src = { 0 };

	struct arr garbage;
	arr_init(&garbage, 16, sizeof(void *));

	while (true) {
		path_join(0, &path, wd.buf, ".editorconfig");
		if (fs_file_exists(path.buf)) {
			struct parse_editorconfig_ctx editorconfig_ctx = {
				.path = path_abs.buf,
			};

			char *cfg_buf = NULL;
			if (!ini_parse(path.buf, &cfg_src, &cfg_buf, editorconfig_cfg_parse_cb, &editorconfig_ctx)) {
				goto ret;
			}

			arr_push(&garbage, &cfg_buf);

			fs_source_destroy(&cfg_src);
			cfg_src = (struct source){ 0 };

			if (editorconfig_ctx.matched) {
				if (!indent_style) {
					indent_style = editorconfig_ctx.indent_style;
				}

				if (!indent_size) {
					indent_size = editorconfig_ctx.indent_size;
				}

				if (!tab_width) {
					tab_width = editorconfig_ctx.tab_width;
				}

				if (!max_line_length) {
					max_line_length = editorconfig_ctx.max_line_length;
				}

				if (!end_of_line) {
					end_of_line = editorconfig_ctx.end_of_line;
				}

				if (!insert_final_newline) {
					insert_final_newline = editorconfig_ctx.insert_final_newline;
				}
			}

			if (editorconfig_ctx.was_root) {
				break;
			}
		}

		if (wd.len == 1) {
			break;
		}

		path_copy(0, &path, wd.buf);
		path_dirname(0, &wd, path.buf);
	}

	if (!indent_style) {
		indent_style = "space";
	}

	if (strcmp(indent_style, "space") == 0) {
		opts->indent_style = fmt_indent_style_space;
	} else if (strcmp(indent_style, "tab") == 0) {
		opts->indent_style = fmt_indent_style_tab;
	}

	if (!tab_width) {
		tab_width = "8";
	}

	if (!indent_size) {
		if (strcmp(indent_style, "tab") == 0) {
			indent_size = "1";
		} else {
			indent_size = "4";
		}
	}

	if (strcmp(indent_size, "tab") == 0) {
		indent_size = tab_width;
	}

	opts->indent_size = strtol(indent_size, 0, 10);
	opts->tab_width = strtol(tab_width, 0, 10);

	if (max_line_length) {
		opts->max_line_len = strtol(max_line_length, 0, 10);
	}

	if (end_of_line) {
		if (strcmp("end_of_line", "cr") == 0) {
			opts->end_of_line = fmt_end_of_line_cr;
		} else if (strcmp("end_of_line", "lf") == 0) {
			opts->end_of_line = fmt_end_of_line_lf;
		} else if (strcmp("end_of_line", "crlf") == 0) {
			opts->end_of_line = fmt_end_of_line_crlf;
		}
	}

	if (insert_final_newline) {
		opts->insert_final_newline = strcmp(insert_final_newline, "false") == 0 ? false : true;
	}

ret:
	for (uint32_t i = 0; i < garbage.len; ++i) {
		z_free(*(void **)arr_get(&garbage, i));
	}
	arr_destroy(&garbage);

	fs_source_destroy(&cfg_src);
	tstr_destroy(&wd);
	tstr_destroy(&path);
	tstr_destroy(&path_abs);
}

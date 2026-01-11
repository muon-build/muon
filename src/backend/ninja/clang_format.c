/*
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "compat.h"

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja/clang_format.h"
#include "backend/output.h"
#include "functions/kernel.h"
#include "lang/object_iterators.h"
#include "lang/serial.h"
#include "lang/string.h"
#include "log.h"
#include "options.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "toolchains.h"

static obj
clang_format_detect(struct workspace *wk)
{
	obj found_clang_format;
	struct find_program_ctx fp_ctx = {
		.res = &found_clang_format,
		.requirement = requirement_auto,
		.machine = machine_kind_build,
	};
	if (!find_program(wk, &fp_ctx, make_str(wk, "clang-format")) || !fp_ctx.found) {
		return 0;
	}

	return found_clang_format;
}

static bool
clang_format_target_name_exists(struct workspace *wk, const struct str *target_name)
{
	uint32_t proj_i;
	for (proj_i = 0; proj_i < wk->projects.len; ++proj_i) {
		const struct project *proj = arr_get(&wk->projects, proj_i);

		obj tgt_id;
		obj_array_for(wk, proj->targets, tgt_id) {
			obj name = ca_backend_tgt_name(wk, tgt_id);
			const struct str *tgt_name = get_str(wk, name);

			if (str_eql(tgt_name, target_name)) {
				return true;
			}
		}
	}

	return false;
}

static obj
clang_format_read_pattern_file(struct workspace *wk, const struct str *filename)
{
	TSTR(pattern_path);
	path_join(wk, &pattern_path, get_cstr(wk, current_project(wk)->source_root), filename->s);

	if (!fs_file_exists(pattern_path.buf)) {
		return 0;
	}

	struct source src;
	if (!fs_read_entire_file(wk->a_scratch, pattern_path.buf, &src)) {
		return 0;
	}

	obj patterns = make_obj(wk, obj_array);
	const char *line_start = src.src;
	const char *line_end;

	while (line_start < src.src + src.len) {
		// Find end of line
		line_end = strchr(line_start, '\n');
		if (!line_end) {
			line_end = src.src + src.len;
		}

		// Skip whitespace at start
		while (line_start < line_end && (*line_start == ' ' || *line_start == '\t')) {
			line_start++;
		}

		// Skip empty lines and comments
		if (line_start < line_end && *line_start != '#' && *line_start != '\n') {
			// Trim trailing whitespace
			const char *trimmed_end = line_end;
			while (trimmed_end > line_start
				&& (*(trimmed_end - 1) == ' ' || *(trimmed_end - 1) == '\t'
					|| *(trimmed_end - 1) == '\r')) {
				trimmed_end--;
			}

			if (trimmed_end > line_start) {
				obj pattern = make_strn(wk, line_start, trimmed_end - line_start);
				obj_array_push(wk, patterns, pattern);
			}
		}

		line_start = line_end;
		if (*line_start == '\n') {
			line_start++;
		}
	}

	return patterns;
}

static bool
clang_format_matches_any_pattern(struct workspace *wk, const struct str *path, obj patterns)
{
	if (!patterns) {
		return false;
	}

	obj pattern;
	obj_array_for(wk, patterns, pattern) {
		const struct str *pat = get_str(wk, pattern);
		if (str_eql_glob(pat, path)) {
			return true;
		}
	}
	return false;
}

struct clang_format_collect_sources_ctx {
	obj file_list;
	obj include_patterns;
	obj ignore_patterns;
	struct tstr *file_list_path;
};

static void
clang_format_collect_source_files_from_list(struct workspace *wk, struct clang_format_collect_sources_ctx *ctx, obj list)
{
	obj file;
	obj_array_for(wk, list, file) {
		const struct str *path = get_str(wk, *get_obj_file(wk, file));

		enum compiler_language l;
		if (!(filename_to_compiler_language(path->s, &l))) {
			continue;
		}

		switch (l) {
		case compiler_language_c:
		case compiler_language_cpp:
		case compiler_language_objc:
		case compiler_language_objcpp:
		case compiler_language_c_hdr:
		case compiler_language_cpp_hdr:
		case compiler_language_objc_hdr:
		case compiler_language_objcpp_hdr:
			break;
		default: continue;
		}

		if (clang_format_matches_any_pattern(wk, path, ctx->ignore_patterns)) {
			continue;
		}

		if (ctx->include_patterns && !clang_format_matches_any_pattern(wk, path, ctx->include_patterns)) {
			continue;
		}

		obj_array_push(wk, ctx->file_list, file);
	}
}

static bool
clang_format_write_file_list(struct workspace *wk, void *_ctx, FILE *out)
{
	obj *file_list = _ctx;
	return serial_dump(wk, *file_list, out);
}

static void
ninja_create_phony_clang_format_target(struct workspace *wk,
	FILE *out,
	const struct str *target_name,
	const struct str *command,
	const struct str *description)
{
	TSTR(desc_escaped);
	ninja_escape(wk, &desc_escaped, description->s);

	obj_fprintf(wk,
		out,
		"build %s: CUSTOM_COMMAND build_always_stale\n"
		" command = %s\n"
		" description = %s\n"
		" pool = console\n\n",
		target_name->s,
		command->s,
		description->s);
}

void
ninja_clang_format_write_targets(struct workspace *wk, FILE *out)
{
	struct clang_format_collect_sources_ctx ctx[1] = { {
		.file_list = make_obj(wk, obj_array),
		.include_patterns = clang_format_read_pattern_file(wk, &STR(".clang-format-include")),
		.ignore_patterns = clang_format_read_pattern_file(wk, &STR(".clang-format-ignore")),
	} };

	{ // collect source files
		uint32_t i;
		for (i = 0; i < wk->projects.len; ++i) {
			struct project *proj = arr_get(&wk->projects, i);

			obj tgt;
			obj_array_for(wk, proj->targets, tgt) {
				struct obj_build_target *t;

				switch (get_obj_type(wk, tgt)) {
				case obj_build_target:
					t = get_obj_build_target(wk, tgt);
					clang_format_collect_source_files_from_list(wk, ctx, t->src);
					break;
				case obj_both_libs: {
					struct obj_both_libs *libs = get_obj_both_libs(wk, tgt);
					t = get_obj_build_target(wk, libs->static_lib);
					clang_format_collect_source_files_from_list(wk, ctx, t->src);
					t = get_obj_build_target(wk, libs->dynamic_lib);
					clang_format_collect_source_files_from_list(wk, ctx, t->src);
					break;
				}
				default: break;
				}
			}
		}

		obj_array_dedup_in_place(wk, &ctx->file_list);
	}

	if (get_obj_array(wk, ctx->file_list)->len == 0) {
		LOG_W("no c/c++ source files found for clang-format");
		return;
	}

	TSTR(file_list_path);
	{
		const char *file_list ="clang-format-files.txt";
		if (!with_open(wk->muon_private, file_list, wk, &ctx, clang_format_write_file_list)) {
			LOG_E("failed to write file list for clang-format");
			return;
		}

		// with_open doesn't communicate what the path was
		path_join(wk, &file_list_path, wk->muon_private, file_list);
	}

	obj clang_format;
	if (!(clang_format = clang_format_detect(wk))) {
		LOG_E("failed to find clang-format");
		return;
	}
	struct obj_external_program *ep = get_obj_external_program(wk, clang_format);
	obj command_base = make_obj(wk, obj_array);
	push_args_null_terminated(wk,
		command_base,
		(char *const[]){
			(char *)wk->argv0,
			"internal",
			"eval",
			"-e",
			"commands/clang_format.meson",
			"-l", file_list_path.buf,
			0,
		});

	{
		obj command;
		obj_array_dup(wk, command_base, &command);
		obj_array_push(wk, command, make_str(wk, "--"));
		obj_array_extend(wk, command, ep->cmd_array);
		obj joined = join_args_shell_ninja(wk, command);

		ninja_create_phony_clang_format_target(
			wk, out, &STR("clang-format"), get_str(wk, joined), &STR("formatting c/c++ files"));
	}

	{
		obj command;
		obj_array_dup(wk, command_base, &command);
		obj_array_push(wk, command, make_str(wk, "-n"));
		obj_array_push(wk, command, make_str(wk, "--"));
		obj_array_extend(wk, command, ep->cmd_array);
		obj joined = join_args_shell_ninja(wk, command);

		ninja_create_phony_clang_format_target(wk,
			out,
			&STR("clang-format-check"),
			get_str(wk, joined),
			&STR("checking c/c++ file formatting"));
	}
}

bool
ninja_clang_format_is_enabled_and_available(struct workspace *wk)
{
	obj opt;
	get_option_value(wk, 0, "b_clang_format", &opt);
	enum feature_opt_state b_clang_format = get_obj_feature_opt(wk, opt);
	if (b_clang_format == feature_opt_disabled) {
		return false;
	} else if (b_clang_format == feature_opt_enabled) {
		return true;
	}

	{ // check config exists
		TSTR(config_path);
		path_join(wk, &config_path, get_cstr(wk, current_project(wk)->source_root), ".clang-format");
		if (!fs_file_exists(config_path.buf)) {
			return false;
		}
	}

	if (!clang_format_detect(wk)) {
		LOG_W(".clang-format found but clang-format tool is not available");
	}

	if (clang_format_target_name_exists(wk, &STR("clang-format")) || clang_format_target_name_exists(wk, &STR("clang-format-check"))) {
		L("clang-format: user-defined clang-format/clang-format-check targets detected, skipping automatic targets");
		return false;
	}

	return true;
}

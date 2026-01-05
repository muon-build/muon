/*
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "compat.h"

#include "backend/ninja/clang_format.h"

#include "backend/common_args.h"
#include "lang/object_iterators.h"
#include "lang/string.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "toolchains.h"

// File extensions for C/C++ files
static const char *c_cpp_extensions[] = {
	".c",
	".cpp",
	".cc",
	".cxx",
	".C",
	".h",
	".hpp",
	".hh",
	".hxx",
	".H",
	NULL,
};

static bool
ninja_clang_format_detect(struct workspace *wk)
{
	bool found = false;
	struct run_cmd_ctx clang_format_ctx = { 0 };
	char *const clang_format_args[] = { "clang-format", "--version", NULL };

	if (!run_cmd_argv(wk, &clang_format_ctx, clang_format_args, NULL, 0) || (clang_format_ctx.status != 0)) {
		goto cleanup;
	}

	LOG_I("found clang-format: %.*s", (int)strcspn(clang_format_ctx.out.buf, "\n"), clang_format_ctx.out.buf);
	found = true;

cleanup:
	run_cmd_ctx_destroy(&clang_format_ctx);
	return found;
}

static bool
check_clang_format_config_exists(struct workspace *wk)
{
	TSTR(config_path);
	path_join(wk, &config_path, get_cstr(wk, current_project(wk)->source_root), ".clang-format");
	return fs_file_exists(config_path.buf);
}

// Check if a target with given name already exists in any project
static bool
target_name_exists(struct workspace *wk, const struct str *target_name)
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

// Read and parse pattern file (.clang-format-include or .clang-format-ignore)
// Returns 0 if file doesn't exist, otherwise returns array of patterns
static obj
read_pattern_file(struct workspace *wk, const struct str *filename)
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

// Check if file matches any pattern in the array
static bool
matches_any_pattern(struct workspace *wk, const struct str *path, obj patterns)
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

// Check if this file should be formatted based on include/ignore patterns
static bool
should_format_file(struct workspace *wk, const struct str *path, obj include_patterns, obj ignore_patterns)
{
	// If ignore patterns match, skip
	if (matches_any_pattern(wk, path, ignore_patterns)) {
		return false;
	}

	// If include patterns specified and don't match, skip
	if (include_patterns && !matches_any_pattern(wk, path, include_patterns)) {
		return false;
	}

	return true;
}

// Check if file has C/C++ extension
static bool
is_clike_file(const struct str *path)
{
	const char *ext = strrchr(path->s, '.');
	if (!ext) {
		return false;
	}

	const char **exts = c_cpp_extensions;
	while (exts != NULL) {
		if (str_eql(&STRL(ext), &STRL(*exts))) {
			return true;
		}
		exts++;
	}
	return false;
}

struct collect_sources_ctx {
	obj file_list;
	obj include_patterns;
	obj ignore_patterns;
};

static enum iteration_result
collect_source_file_iter(struct workspace *wk, void *_ctx, obj file_obj)
{
	struct collect_sources_ctx *ctx = _ctx;
	const char *path_s = get_file_path(wk, file_obj);
	const struct str path = STRL(path_s);

	// Skip if not a C/C++ file
	if (!is_clike_file(&path)) {
		return ir_cont;
	}

	// Check patterns
	if (!should_format_file(wk, &path, ctx->include_patterns, ctx->ignore_patterns)) {
		return ir_cont;
	}

	// Add to list (avoiding duplicates)
	obj existing;
	obj_array_for(wk, ctx->file_list, existing) {
		const char *existing_path = get_file_path(wk, existing);
		if (str_eql(&path, &STRL(existing_path))) {
			return ir_cont;
		}
	}

	obj_array_push(wk, ctx->file_list, file_obj);
	return ir_cont;
}

// Collect all C/C++ source files from build targets
static obj
collect_source_files(struct workspace *wk, obj include_patterns, obj ignore_patterns)
{
	obj file_list = make_obj(wk, obj_array);

	struct collect_sources_ctx ctx = {
		.file_list = file_list,
		.include_patterns = include_patterns,
		.ignore_patterns = ignore_patterns,
	};

	// Iterate through all projects
	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = arr_get(&wk->projects, i);

		// Iterate through all targets in the project
		obj tgt;
		obj_array_for(wk, proj->targets, tgt) {
			struct obj_build_target *t;

			switch (get_obj_type(wk, tgt)) {
			case obj_build_target:
				t = get_obj_build_target(wk, tgt);
				// Iterate through source files
				obj_array_foreach(wk, t->src, &ctx, collect_source_file_iter);
				break;
			case obj_both_libs: {
				struct obj_both_libs *libs = get_obj_both_libs(wk, tgt);
				t = get_obj_build_target(wk, libs->static_lib);
				obj_array_foreach(wk, t->src, &ctx, collect_source_file_iter);
				t = get_obj_build_target(wk, libs->dynamic_lib);
				obj_array_foreach(wk, t->src, &ctx, collect_source_file_iter);
				break;
			}
			default:
				// Skip non-build targets (custom targets, aliases, etc.)
				break;
			}
		}
	}

	return file_list;
}

// Write files to a temporary list file for clang-format
static bool
write_file_list(struct workspace *wk, obj file_list, struct tstr *list_file_path)
{
	// Create a temporary file in the build directory
	TSTR(temp_path);
	path_join(wk, &temp_path, wk->build_root, "meson-private");
	if (!fs_mkdir(temp_path.buf, true)) {
		return false;
	}

	path_push(wk, &temp_path, "clang-format-files.txt");
	tstr_pushs(wk, list_file_path, temp_path.buf);

	FILE *f = fs_fopen(temp_path.buf, "w");
	if (!f) {
		LOG_E("Failed to create file list: %s", temp_path.buf);
		return false;
	}

	obj file;
	obj_array_for(wk, file_list, file) {
		obj_fprintf(wk, f, "%s\n", get_file_path(wk, file));
	}

	fclose(f);
	return true;
}

static void
ninja_create_phony_clang_format_target(struct workspace *wk,
	FILE *out,
	const struct str *target_name,
	const struct str *command,
	const struct str *description)
{
	obj_fprintf(wk, out, "build %s: phony muon-internal__%s\n\n", target_name->s, target_name->s);
	obj_fprintf(wk,
		out,
		"build muon-internal__%s: CUSTOM_COMMAND build_always_stale\n"
		" command = %s\n"
		" description = %s\n"
		" pool = console\n\n",
		target_name->s,
		command->s,
		description->s);
}

static void
ninja_clang_format_write_format_target(struct workspace *wk, FILE *out)
{
	// Read pattern files
	obj include_patterns = read_pattern_file(wk, &STR(".clang-format-include"));
	obj ignore_patterns = read_pattern_file(wk, &STR(".clang-format-ignore"));

	// Collect source files programmatically
	obj file_list = collect_source_files(wk, include_patterns, ignore_patterns);

	if (get_obj_array(wk, file_list)->len == 0) {
		LOG_W("No C/C++ source files found for clang-format");
		return;
	}

	// Write file list to temporary file
	TSTR(list_file);
	if (!write_file_list(wk, file_list, &list_file)) {
		LOG_E("Failed to write file list for clang-format");
		return;
	}

	// Build command that reads from file list
	TSTR(command);
	tstr_pushf(wk, &command, "cat %s | xargs -d '\\n' clang-format -i", list_file.buf);

	ninja_create_phony_clang_format_target(
		wk, out, &STR("clang-format"), &STRL(command.buf), &STR("Formatting$ C/C++$ files"));
}

static void
ninja_clang_format_write_check_target(struct workspace *wk, FILE *out)
{
	// Read pattern files
	obj include_patterns = read_pattern_file(wk, &STR(".clang-format-include"));
	obj ignore_patterns = read_pattern_file(wk, &STR(".clang-format-ignore"));

	// Collect source files programmatically
	obj file_list = collect_source_files(wk, include_patterns, ignore_patterns);

	if (get_obj_array(wk, file_list)->len == 0) {
		return;
	}

	// Write file list to temporary file
	TSTR(list_file);
	if (!write_file_list(wk, file_list, &list_file)) {
		return;
	}

	// Build command that reads from file list
	TSTR(command);
	tstr_pushf(wk, &command, "cat %s | xargs -d '\\n' clang-format --dry-run --Werror", list_file.buf);

	ninja_create_phony_clang_format_target(
		wk, out, &STR("clang-format-check"), &STRL(command.buf), &STR("Checking$ C/C++$ file$ formatting"));
}

void
ninja_clang_format_write_targets(struct workspace *wk, FILE *out)
{
	ninja_clang_format_write_format_target(wk, out);
	ninja_clang_format_write_check_target(wk, out);
	LOG_I("clang-format targets generated");
}

bool
ninja_clang_format_is_enabled_and_available(struct workspace *wk)
{
	if (!check_clang_format_config_exists(wk)) {
		return false;
	}

	if (!ninja_clang_format_detect(wk)) {
		LLOG_W(".clang-format found but clang-format tool is not available\n");
		return false;
	}

	if (target_name_exists(wk, &STR("clang-format")) || target_name_exists(wk, &STR("clang-format-check"))) {
		L("clang-format: user-defined clang-format/clang-format-check targets detected, skipping automatic targets");
		return false;
	}

	return true;
}

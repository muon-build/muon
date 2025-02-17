/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "args.h"
#include "functions/string.h"
#include "log.h"
#include "options.h"
#include "backend/ninja/coverage.h"
#include "platform/run_cmd.h"
#include "platform/path.h"

#include <string.h>

static bool
ninja_coverage_detect_gcovr(struct workspace *wk)
{
	bool found = false;
	struct run_cmd_ctx gcovr_ctx = { 0 };
	char *const gcovr_args[] = { "gcovr", "--version", NULL };

	// TODO: This should be find_program(), but that interface needs to be refactored first.
	if (!run_cmd_argv(&gcovr_ctx, gcovr_args, NULL, 0) || (gcovr_ctx.status != 0)) {
		goto cleanup;
	}

	LOG_I("found gcovr: %.*s", (int)strcspn(gcovr_ctx.out.buf, "\n"), gcovr_ctx.out.buf);
	found = true;

cleanup:
	run_cmd_ctx_destroy(&gcovr_ctx);
	return found;
}

static void
ninja_coverage_write_phony_clean_target(struct workspace *wk, FILE *out)
{
	obj cmdline;
	cmdline = make_obj(wk, obj_array);

	push_args_null_terminated(wk,
		cmdline,
		(char *const[]){
			(char *)wk->argv0,
			"internal",
			"exe",
			"ninja",
			"-t",
			"clean",
			NULL,
		}
	);

	fprintf(out, "build clean: phony muon-internal__clean\n");

	cmdline = join_args_shell_ninja(wk, cmdline);
	fprintf(out, "build muon-internal__clean: CUSTOM_COMMAND build_always_stale | clean-gcda clean-gcno\n"
		" command = %s\n"
		" description = Cleaning\n\n",
		get_cstr(wk, cmdline));
}

static obj
ninja_coverage_base_cmdline(struct workspace *wk)
{
	obj cmdline;
	cmdline = make_obj(wk, obj_array);

	TSTR(subprojects_path);
	path_join(wk,
		&subprojects_path,
		get_cstr(wk, current_project(wk)->source_root),
		get_cstr(wk, current_project(wk)->subprojects_dir));

	push_args_null_terminated(wk,
		cmdline,
		(char *const[]){
			(char *)wk->argv0,
			"internal",
			"eval",
			"-e",
			"commands/coverage.meson",
			(char *)get_cstr(wk, current_project(wk)->source_root),
			subprojects_path.buf,
			(char *)get_cstr(wk, current_project(wk)->build_root),
			(char *)NULL,
		}
	);

	return cmdline;
}

static void
ninja_create_phony_target(struct workspace *wk, FILE *out,
						  const char *target_name, const char *command,
						  const char *description)
{
	fprintf(out, "build %s: phony muon-internal__%s\n\n", target_name, target_name);
	fprintf(out, "build muon-internal__%s: CUSTOM_COMMAND build_always_stale\n"
		" command = %s\n"
		" description = %s\n\n",
		target_name, command, description);
}

static void
ninja_coverage_write_coverage_target(struct workspace *wk, FILE *out,
									 const char *target_name,
									 const char *script_arg,
									 const char *target_description)
{
	obj cmdline = ninja_coverage_base_cmdline(wk);
	if (script_arg != NULL) {
		push_args_null_terminated(wk, cmdline,
			(char *const[]) {
				(char *const)script_arg,
				NULL
			}
		);
	}

	cmdline = join_args_shell_ninja(wk, cmdline);
	ninja_create_phony_target(wk, out,
							  target_name,
							  get_cstr(wk, cmdline),
							  target_description);
}

static void
ninja_coverage_write_coverage_targets(struct workspace *wk, FILE *out)
{
	ninja_coverage_write_coverage_target(wk, out,
		"coverage", NULL, "Generating coverage reports");
	ninja_coverage_write_coverage_target(wk, out,
		"coverage-html", "html", "Generating HTML coverage report");
	ninja_coverage_write_coverage_target(wk, out,
		"coverage-xml", "xml", "Generating XML coverage report");
	ninja_coverage_write_coverage_target(wk, out,
		"coverage-text", "text", "Generating text coverage report");
	ninja_coverage_write_coverage_target(wk, out,
		"coverage-sonarqube", "sonarqube", "Generating sonarqube coverage report");
}

static void
ninja_write_recursive_delete_target(struct workspace *wk, FILE *out,
									const char *target_name, const char *suffix)
{
	obj cmdline;
	cmdline = make_obj(wk, obj_array);

	push_args_null_terminated(wk,
		cmdline,
		(char *const[]){
			(char *)wk->argv0,
			"internal",
			"eval",
			"-e",
			"commands/delete_suffix.meson",
			(char *)wk->build_root,
			(char *)suffix,
			NULL
		}
	);

	fprintf(out, "build %s: phony muon-internal__%s\n", target_name, target_name);

	cmdline = join_args_shell_ninja(wk, cmdline);
	fprintf(out, "build muon-internal__%s: CUSTOM_COMMAND build_always_stale\n"
		" command = %s\n"
		" description = Deleting$ %s$ files\n\n",
		target_name,
		get_cstr(wk, cmdline),
		suffix);
}

static void
ninja_coverage_write_cleanup_targets(struct workspace *wk, FILE *out)
{
	ninja_write_recursive_delete_target(wk, out, "clean-gcda", ".gcda");
	ninja_write_recursive_delete_target(wk, out, "clean-gcno", ".gcno");
}

void
ninja_coverage_write_targets(struct workspace *wk, FILE *out)
{
	ninja_coverage_write_coverage_targets(wk, out);
	ninja_coverage_write_phony_clean_target(wk, out);
	ninja_coverage_write_cleanup_targets(wk, out);

	LOG_I("coverage targets generated");
}

bool
ninja_coverage_is_enabled_and_available(struct workspace *wk)
{
	obj coverage_option;
	get_option_value(wk, NULL, "b_coverage", &coverage_option);
	if (!get_obj_bool(wk, coverage_option)) {
		return false;
	}

	// Find coverage tools. Initial coverage support assumes gcovr. Meson
	// supports a wider range of tools but at increased complexity. If
	// gcovr is not available, warn user and don't generate coverage targets.
	if (!ninja_coverage_detect_gcovr(wk)) {
		LLOG_W("Coverage is enabled (b_coverage=true) but coverage tools (gcovr) were not found\n");
		return false;
	}

	return true;
}

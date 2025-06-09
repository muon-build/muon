/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_WRAP_H
#define MUON_WRAP_H

#include "compat.h"

#include <stdbool.h>
#include <stdint.h>

#include "buf_size.h"
#include "lang/string.h"
#include "platform/filesystem.h"
#include "platform/run_cmd.h"
#include "platform/timer.h"

struct workspace;

enum wrap_fields {
	// wrap
	wf_directory,
	wf_patch_url,
	wf_patch_fallback_url,
	wf_patch_filename,
	wf_patch_hash,
	wf_patch_directory,
	wf_diff_files,
	wf_method,

	// wrap-file
	wf_source_url,
	wf_source_fallback_url,
	wf_source_filename,
	wf_source_hash,
	wf_lead_directory_missing,

	// wrap-git
	wf_url,
	wf_revision,
	wf_depth,
	wf_push_url,
	wf_clone_recursive,

	wf_wrapdb_version, // ?? undocumented

	wrap_fields_count,
};

enum wrap_type {
	wrap_type_file,
	wrap_type_git,
	wrap_provide,
	wrap_type_count,
};

struct wrap {
	struct source src;
	enum wrap_type type;
	bool has_provides;
	const char *fields[wrap_fields_count];
	char *buf;
	char dest_dir_buf[BUF_SIZE_1k], name_buf[BUF_SIZE_1k];
	struct tstr dest_dir, name;
	bool dirty, outdated, updated;
};

enum wrap_provides_key {
	wrap_provides_key_override_dependencies,
	wrap_provides_key_override_executables,
	wrap_provides_key_dependency_variables,
};

enum wrap_handle_mode {
	wrap_handle_mode_default,
	wrap_handle_mode_check_dirty,
	wrap_handle_mode_update,
};

struct wrap_opts {
	const char *subprojects;
	bool allow_download, force_update;
	enum wrap_handle_mode mode;
	bool block;
};

enum wrap_handle_sub_state {
	wrap_handle_sub_state_pending,
	wrap_handle_sub_state_running,
	wrap_handle_sub_state_running_cmd,
	wrap_handle_sub_state_fetching,
	wrap_handle_sub_state_extracting,
	wrap_handle_sub_state_complete,
	wrap_handle_sub_state_collected,
};

struct wrap_handle_ctx {
	struct wrap_opts opts;
	struct wrap wrap;
	struct timer duration;

	uint32_t prev_state, state;
	enum wrap_handle_sub_state sub_state;

	const char *path;

	struct {
		int64_t depth;
		char depth_str[16];
	} git;

	struct {
		int32_t handle;
		uint8_t *buf;
		uint64_t len;
		int64_t downloaded, total;
		const char *hash, *dest_dir, *filename;
	} fetch_ctx;

	struct run_cmd_ctx cmd_ctx;
	struct {
		char cmdstr[1024]; // For error reporting
		struct tstr *out;
		bool allow_failure;
	} run_cmd_opts;

	char tstr_buf[2][1024];
	struct tstr bufs[2];

	bool ok;
};

void wrap_destroy(struct wrap *wrap);
bool wrap_parse(struct workspace *wk, const char *subprojects, const char *wrap_file, struct wrap *wrap);
bool wrap_handle(struct workspace *wk, const char *wrap_file, struct wrap_handle_ctx *ctx);
void wrap_handle_async_start(struct workspace *wk);
void wrap_handle_async_end(struct workspace *wk);
bool wrap_handle_async(struct workspace *wk, const char *wrap_file, struct wrap_handle_ctx *ctx);
bool wrap_load_all_provides(struct workspace *wk, const char *subprojects);
const char *wrap_handle_state_to_s(uint32_t state);
#endif

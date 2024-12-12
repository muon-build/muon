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
#include "lang/types.h"
#include "platform/filesystem.h"

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
	struct sbuf dest_dir, name;
	bool dirty, outdated;
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
};

void wrap_destroy(struct wrap *wrap);
bool wrap_parse(const char *wrap_file, struct wrap *wrap);
bool wrap_handle(const char *wrap_file, struct wrap *wrap, struct wrap_opts *opts);
bool wrap_load_all_provides(struct workspace *wk, const char *subprojects);
#endif

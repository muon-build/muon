#ifndef MUON_WRAP_H
#define MUON_WRAP_H

#include "posix.h"

#include <stdbool.h>
#include <stdint.h>

#include "buf_size.h"
#include "platform/filesystem.h"

struct workspace;

enum wrap_fields {
	// wrap
	wf_directory, // ignored
	wf_patch_url,
	wf_patch_fallback_url, // ignored
	wf_patch_filename,
	wf_patch_hash,
	wf_patch_directory, // ignored

	// wrap-file
	wf_source_url,
	wf_source_fallback_url, // ignored
	wf_source_filename,
	wf_source_hash,
	wf_lead_directory_missing, // ignored

	// wrap-git
	wf_url, // ignored
	wf_revision, // ignored
	wf_depth, // ignored
	wf_push_url, // ignored
	wf_clone_recursive, // ignored

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
	char dest_dir[PATH_MAX];
	const char *fields[wrap_fields_count];
	char *buf;
};

void wrap_destroy(struct wrap *wrap);
bool wrap_parse(const char *wrap_file, struct wrap *wrap);
bool wrap_handle(const char *wrap_file, const char *subprojects, struct wrap *wrap);
#endif

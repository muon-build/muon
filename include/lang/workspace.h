#ifndef MUON_LANG_WORKSPACE_H
#define MUON_LANG_WORKSPACE_H

#include "posix.h"

#include "buf_size.h"
#include "data/bucket_array.h"
#include "data/darr.h"
#include "data/hash.h"
#include "lang/eval.h"
#include "lang/object.h"
#include "lang/parser.h"
#include "lang/string.h"

// TODO: make obj_project and then that can be this.
struct project {
	struct hash scope;

	obj source_root, build_root, cwd, build_dir, subproject_name;
	obj opts, compilers, targets, tests, summary;
	obj args, link_args;

	struct {
		obj name;
		obj version;
		obj license;
	} cfg;
};

enum loop_ctl {
	loop_norm,
	loop_breaking,
	loop_continuing,
};

struct option_override {
	uint32_t proj, name, val;
	bool obj_value;
};

enum {
	disabler_id = 1
};

struct workspace {
	char argv0[PATH_MAX],
	     source_root[PATH_MAX],
	     build_root[PATH_MAX],
	     muon_private[PATH_MAX];

	/* Global objects
	 * These should probably be cleaned up into a separate struct.
	 */
	/* ----------------- */
	/* obj_array that tracks each source file eval'd */
	obj sources;
	/* TODO host machine dict */
	obj host_machine;
	/* TODO binaries dict */
	obj binaries;
	obj install;
	obj install_scripts;
	obj subprojects;
	/* args dict for add_global_arguments() */
	obj global_args;
	/* args dict for add_global_link_arguments() */
	obj global_link_args;
	/* overridden dependencies dict */
	obj dep_overrides;
	obj subprojects_dir;
	/* ----------------- */

	struct bucket_array chrs;
	struct bucket_array objs;
	struct bucket_array obj_aos[obj_type_count - _obj_aos_start];

	struct darr projects;
	struct darr option_overrides;
	struct darr source_data;

	struct hash scope;
	struct hash obj_hash;

	uint32_t stack_depth, loop_depth;
	enum loop_ctl loop_ctl;
	bool subdir_done;

	uint32_t cur_project;

	/* ast of current file */
	struct ast *ast;
	/* source of current file */
	struct source *src;

	enum language_mode lang_mode;
};

bool get_obj_id(struct workspace *wk, const char *name, obj *res, uint32_t proj_id);

void workspace_init_bare(struct workspace *wk);
void workspace_init(struct workspace *wk);
void workspace_destroy_bare(struct workspace *wk);
void workspace_destroy(struct workspace *wk);
bool workspace_setup_dirs(struct workspace *wk, const char *build, const char *argv0, bool mkdir);

struct obj_install_target *push_install_target(struct workspace *wk, obj src,
	obj dest, obj mode);
struct obj_install_target *push_install_target_install_dir(struct workspace *wk,
	obj src, obj install_dir, obj mode);
struct obj_install_target *push_install_target_basename(struct workspace *wk,
	obj base_path, obj filename, obj install_dir, obj mode);
bool push_install_targets(struct workspace *wk, obj filenames,
	obj install_dirs, obj install_mode);

struct project *make_project(struct workspace *wk, uint32_t *id, const char *subproject_name,
	const char *cwd, const char *build_dir);
struct project *current_project(struct workspace *wk);
void workspace_print_summaries(struct workspace *wk);

//TODO: ?
const char *wk_file_path(struct workspace *wk, uint32_t id);
#endif

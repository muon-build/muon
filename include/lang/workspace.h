#ifndef MUON_LANG_WORKSPACE_H
#define MUON_LANG_WORKSPACE_H

#include "posix.h"

#include <limits.h>

#include "data/bucket_array.h"
#include "data/darr.h"
#include "data/hash.h"
#include "lang/eval.h"
#include "lang/object.h"
#include "lang/parser.h"
#include "lang/string.h"

struct project {
	struct hash scope;

	str source_root, build_root, cwd, build_dir, subproject_name;
	obj opts, compilers, targets, tests, summary;
	obj args, link_args;

	struct {
		str name;
		str version;
		str license;
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
	disabler_id = 1 << 1
};

struct workspace {
	char argv0[PATH_MAX],
	     source_root[PATH_MAX],
	     build_root[PATH_MAX],
	     muon_private[PATH_MAX];

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

	struct bucket_array chrs;
	struct bucket_array strs;
	struct bucket_array objs;

	struct darr projects;
	struct darr option_overrides;
	struct darr source_data;

	struct hash scope;

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

struct obj *make_obj(struct workspace *wk, uint32_t *id, enum obj_type type);
uint32_t make_str(struct workspace *wk, const char *str);
struct obj *get_obj(struct workspace *wk, uint32_t id);
bool get_obj_id(struct workspace *wk, const char *name, uint32_t *id, uint32_t proj_id);

void workspace_init_bare(struct workspace *wk);
void workspace_init(struct workspace *wk);
void workspace_destroy_bare(struct workspace *wk);
void workspace_destroy(struct workspace *wk);
bool workspace_setup_dirs(struct workspace *wk, const char *build, const char *argv0, bool mkdir);

struct obj *push_install_target(struct workspace *wk, obj src, obj dest, obj mode);
struct obj *push_install_target_install_dir(struct workspace *wk, obj src,
	obj install_dir, obj mode);
struct obj *push_install_target_basename(struct workspace *wk, obj base_path, obj filename,
	obj install_dir, obj mode);
bool push_install_targets(struct workspace *wk, obj filenames,
	obj install_dirs, obj install_mode);

struct project *make_project(struct workspace *wk, uint32_t *id, const char *subproject_name,
	const char *cwd, const char *build_dir);
struct project *current_project(struct workspace *wk);
void workspace_print_summaries(struct workspace *wk);

//TODO: ?
const char *wk_file_path(struct workspace *wk, uint32_t id);
#endif

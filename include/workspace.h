#ifndef MUON_WORKSPACE_H
#define MUON_WORKSPACE_H

#include "darr.h"
#include "eval.h"
#include "hash.h"
#include "object.h"
#include "parser.h"

struct project {
	struct hash scope;
	struct darr tokens;
	/* wk_strings */
	uint32_t cwd, build_dir, subproject_name;
	/* objects */
	uint32_t opts;

	struct {
		uint32_t name;
		uint32_t version;
		uint32_t license;
		uint32_t meson_version;
		uint32_t args;
	} cfg;

	uint32_t targets;
	uint32_t tests;
};

enum loop_ctl {
	loop_norm,
	loop_breaking,
	loop_continuing,
};

struct option_override {
	const char *proj, *name, *val;
	bool used;
};

struct workspace {
	struct darr projects;
	struct darr option_overrides;
	struct darr objs;
	struct darr strs;
	struct hash scope;

	uint32_t loop_depth;
	enum loop_ctl loop_ctl;
	uint32_t cur_project;
	struct ast *ast;
	enum language_mode lang_mode;
	const char *argv0, *source_root, *build_root;
	const char *cur_src_path;
	char *strbuf;
	uint32_t strbuf_cap;
};

struct obj *make_obj(struct workspace *wk, uint32_t *id, enum obj_type type);
struct obj *get_obj(struct workspace *wk, uint32_t id);
bool get_obj_id(struct workspace *wk, const char *name, uint32_t *id, uint32_t proj);

uint32_t wk_str_pushf(struct workspace *wk, const char *fmt, ...)  __attribute__ ((format(printf, 2, 3)));
char *wk_str(struct workspace *wk, uint32_t id);
void wk_str_appf(struct workspace *wk, uint32_t *id, const char *fmt, ...)  __attribute__ ((format(printf, 3, 4)));
void wk_str_app(struct workspace *wk, uint32_t *id, const char *str);
void wk_str_appn(struct workspace *wk, uint32_t *id, const char *str, uint32_t n);
uint32_t wk_str_push(struct workspace *wk, const char *str);
uint32_t wk_str_pushn(struct workspace *wk, const char *str, uint32_t n);
char *wk_objstr(struct workspace *wk, uint32_t id);
char *wk_file_path(struct workspace *wk, uint32_t id);
uint32_t wk_str_push_stripped(struct workspace *wk, const char *s);
uint32_t wk_str_split(struct workspace *wk, const char *s, const char *sep);

bool prefix_len(const char *str, const char *prefix, uint32_t *len);
uint32_t longest_prefix_len(const char *str, const char *prefix[]);

void workspace_init(struct workspace *wk);
void workspace_destroy(struct workspace *wk);
struct project *make_project(struct workspace *wk, uint32_t *id, const char *subproject_name,
	const char *cwd, const char *build_dir);
struct project *current_project(struct workspace *wk);
#endif

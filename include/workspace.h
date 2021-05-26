#ifndef BOSON_WORKSPACE_H
#define BOSON_WORKSPACE_H

#include "darr.h"
#include "eval.h"
#include "hash.h"
#include "object.h"
#include "parser.h"

struct project {
	uint32_t cwd, build_dir;

	struct {
		uint32_t name;
		uint32_t version;
		uint32_t license;
		uint32_t meson_version;
		uint32_t args;
	} cfg;

	struct hash scope;
	uint32_t targets;
	struct tokens toks;
	struct ast ast;
};

enum loop_ctl {
	loop_norm,
	loop_breaking,
	loop_continuing,
};

struct workspace {
	uint32_t cur_project;
	struct darr projects;
	struct darr objs;
	struct darr strs;
	struct hash scope;

	uint32_t loop_depth;
	enum loop_ctl loop_ctl;
	struct ast *ast;
	enum language_mode lang_mode;
};

struct obj *make_obj(struct workspace *wk, uint32_t *id, enum obj_type type);
struct obj *get_obj(struct workspace *wk, uint32_t id);
bool get_obj_id(struct workspace *wk, const char *name, uint32_t *id, uint32_t proj);

uint32_t wk_str_pushf(struct workspace *wk, const char *fmt, ...)  __attribute__ ((format(printf, 2, 3)));
char *wk_str(struct workspace *wk, uint32_t id);
void wk_strappf(struct workspace *wk, uint32_t *id, const char *fmt, ...)  __attribute__ ((format(printf, 3, 4)));
uint32_t wk_str_push(struct workspace *wk, const char *str);
const char *wk_objstr(struct workspace *wk, uint32_t id);

void workspace_init(struct workspace *wk);
struct project *make_project(struct workspace *wk, uint32_t *id);
struct project *current_project(struct workspace *wk);
#endif

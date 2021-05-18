#ifndef BOSON_WORKSPACE_H
#define BOSON_WORKSPACE_H

#include "darr.h"
#include "hash.h"
#include "object.h"

struct workspace {
	const char *cwd, *build_dir;

	struct {
		const char *name;
		const char *version;
		const char *license;
		const char *meson_version;
		uint32_t args;
	} project;

	struct hash obj_names;
	struct darr objs;
	struct darr strs;
	struct darr tgts;
};

struct obj *make_obj(struct workspace *wk, uint32_t *id, enum obj_type type);
struct obj *get_obj_by_name(struct workspace *wk, const char *name);
struct obj *get_obj(struct workspace *wk, uint32_t id);
bool get_obj_id(struct workspace *wk, const char *name, uint32_t *id);
uint32_t wk_str_pushf(struct workspace *wk, const char *fmt, ...);
char *wk_str(struct workspace *wk, uint32_t id);
void wk_strapp(struct workspace *wk, uint32_t *id, const char *fmt, ...);
void workspace_init(struct workspace *wk);
#endif

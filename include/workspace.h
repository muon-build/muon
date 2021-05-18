#ifndef BOSON_WORKSPACE_H
#define BOSON_WORKSPACE_H

#include "darr.h"
#include "hash.h"
#include "object.h"

struct workspace {
	struct {
		const char *name;
		const char *version;
		const char *license;
		const char *meson_version;
	} project;

	struct hash obj_names;
	struct darr objs;
	struct darr strs;
};

struct obj *make_obj(struct workspace *wk, uint32_t *id, enum obj_type type);
struct obj *get_obj_by_name(struct workspace *wk, const char *name);
struct obj *get_obj(struct workspace *wk, uint32_t id);
bool get_obj_id(struct workspace *wk, const char *name, uint32_t *id);
#endif

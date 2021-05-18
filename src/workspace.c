#include "posix.h"

#include "workspace.h"
#include "interpreter.h"
#include "log.h"

struct obj *
get_obj(struct workspace *wk, uint32_t id)
{
	return darr_get(&wk->objs, id);
}

bool
get_obj_id(struct workspace *wk, const char *name, uint32_t *id)
{
	uint64_t *idp;
	if ((idp = hash_get(&wk->obj_names, name))) {
		*id = *idp;
		return true;
	} else {
		LOG_W(log_interp, "unknown object name '%s'", name);
		return false;
	}
}

struct obj *
get_obj_by_name(struct workspace *wk, const char *name)
{
	uint32_t id;
	if (!get_obj_id(wk, name, &id)) {
		return NULL;
	}

	return get_obj(wk, id);
}

struct obj *
make_obj(struct workspace *wk, uint32_t *id, enum obj_type type)
{
	*id = darr_push(&wk->objs, &(struct obj){ .type = type });
	return darr_get(&wk->objs, *id);
}

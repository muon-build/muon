#include "posix.h"

#include <libpkgconf/libpkgconf.h>
#include <string.h>

#include "buf_size.h"
#include "external/libpkgconf.h"
#include "functions/default/options.h"
#include "lang/object.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

const bool have_libpkgconf = true;

static pkgconf_client_t client;
static pkgconf_cross_personality_t *personality;
static const int maxdepth = 200;

static bool init = false;

static bool
error_handler(const char *msg, const pkgconf_client_t *client, const void *data)
{
	if (log_should_print(log_debug)) {
		log_plain("dbg libpkgconf: %s", msg);
	}
	return true;
}

void
muon_pkgconf_init(struct workspace *wk)
{
	// HACK: TODO: libpkgconf breaks if you try use it after deiniting a
	// client.  Also there are memory leaks abound.
	if (init) {
		return;
	}

	personality = pkgconf_cross_personality_default();
	pkgconf_client_init(&client, error_handler, NULL, personality);

	obj opt;
	get_option(wk, current_project(wk), "pkg_config_path", &opt);
	const struct str *pkg_config_path = get_str(wk, opt);

	if (pkg_config_path->len) {
		pkgconf_path_split(pkg_config_path->s, &client.dir_list, true);
	} else {
		// pkgconf_client_dir_list_build uses PKG_CONFIG_PATH and
		// PKG_CONFIG_LIBDIR from the environment, as well as the
		// builtin path (personality->dir_list).
		//
		// Leaving this here just in case it ever looks like that is a
		// bad idea.
		// pkgconf_path_copy_list(&client.dir_list, &personality->dir_list);
		pkgconf_client_dir_list_build(&client, personality);
	}

	init = true;
}

void
muon_pkgconf_deinit(void)
{
	return;
	pkgconf_path_free(&personality->dir_list);
	pkgconf_path_free(&personality->filter_libdirs);
	pkgconf_path_free(&personality->filter_includedirs);
	pkgconf_client_deinit(&client);
}

static const char *
pkgconf_strerr(int err)
{
	switch (err) {
	case PKGCONF_PKG_ERRF_OK:
		return "ok";
	case PKGCONF_PKG_ERRF_PACKAGE_NOT_FOUND:
		return "not found";
	case PKGCONF_PKG_ERRF_PACKAGE_VER_MISMATCH:
		return "ver mismatch";
	case PKGCONF_PKG_ERRF_PACKAGE_CONFLICT:
		return "package conflict";
	case PKGCONF_PKG_ERRF_DEPGRAPH_BREAK:
		return "depgraph break";
	}

	return "unknown";
}


typedef unsigned int (*apply_func)(pkgconf_client_t *client,
	pkgconf_pkg_t *world, pkgconf_list_t *list, int maxdepth);

struct pkgconf_lookup_ctx {
	apply_func apply_func;
	struct workspace *wk;
	struct pkgconf_info *info;
	obj libdirs;
	obj name;
	bool is_static;
};

struct find_lib_path_ctx {
	bool is_static;
	bool found;
	const char *name;
	char *buf;
};

static bool
check_lib_path(struct find_lib_path_ctx *ctx, const char *lib_path)
{
	enum ext { ext_a, ext_so, ext_count };
	static const char *ext[] = { [ext_a] = ".a", [ext_so] = ".so" };
	static const uint8_t ext_order_static[] = { ext_a, ext_so },
			     ext_order_dynamic[] = { ext_so, ext_a },
			     *ext_order;

	if (ctx->is_static) {
		ext_order = ext_order_static;
	} else {
		ext_order = ext_order_dynamic;
	}

	uint32_t i;

	char name[PATH_MAX];
	for (i = 0; i < ext_count; ++i) {
		snprintf(name, PATH_MAX - 1, "lib%s%s", ctx->name, ext[ext_order[i]]);

		if (!path_join(ctx->buf, PATH_MAX, lib_path, name)) {
			return false;
		}

		if (fs_file_exists(ctx->buf)) {
			ctx->found = true;
			return true;
		}
	}

	return false;
}

static enum iteration_result
find_lib_path_iter(struct workspace *wk, void *_ctx, obj val_id)
{
	struct find_lib_path_ctx *ctx = _ctx;

	if (check_lib_path(ctx, get_cstr(wk, val_id))) {
		return ir_done;
	}

	return ir_cont;
}

static const char *
find_lib_path(pkgconf_client_t *client, struct pkgconf_lookup_ctx *ctx, const char *name)
{
	static char buf[PATH_MAX + 1];
	struct find_lib_path_ctx find_lib_path_ctx = { .buf = buf, .name = name, .is_static = ctx->is_static };
	pkgconf_node_t *node;
	pkgconf_path_t *path;

	PKGCONF_FOREACH_LIST_ENTRY(client->filter_libdirs.head, node) {
		path = node->data;

		if (check_lib_path(&find_lib_path_ctx, path->path)) {
			return buf;
		}
	}

	if (!obj_array_foreach(ctx->wk, ctx->libdirs, &find_lib_path_ctx, find_lib_path_iter)) {
		return NULL;
	} else if (!find_lib_path_ctx.found) {
		return NULL;
	}

	return buf;
}

static bool
apply_and_collect(pkgconf_client_t *client, pkgconf_pkg_t *world, void *_ctx, int maxdepth)
{
	struct pkgconf_lookup_ctx *ctx = _ctx;
	int err;
	pkgconf_node_t *node;
	pkgconf_list_t list = PKGCONF_LIST_INITIALIZER;
	obj str;
	bool ret = true;

	err = ctx->apply_func(client, world, &list, maxdepth);
	if (err != PKGCONF_PKG_ERRF_OK) {
		LOG_E("apply_func failed: %s", pkgconf_strerr(err));
		ret = false;
		goto ret;
	}

	PKGCONF_FOREACH_LIST_ENTRY(list.head, node) {
		const pkgconf_fragment_t *frag = node->data;

		/* L("got option: -'%c' '%s'", frag->type, frag->data); */

		switch (frag->type) {
		case 'I':
			if (!pkgconf_fragment_has_system_dir(client, frag)) {
				make_obj(ctx->wk, &str, obj_include_directory);
				struct obj_include_directory *o = get_obj_include_directory(ctx->wk, str);
				o->path = make_str(ctx->wk, frag->data);
				o->is_system = false;
				obj_array_push(ctx->wk, ctx->info->includes, str);
			}
			break;
		case 'L':
			str = make_str(ctx->wk, frag->data);
			obj_array_push(ctx->wk, ctx->libdirs, str);
			break;
		case 'l': {
			const char *path;
			if ((path = find_lib_path(client, ctx, frag->data))) {
				str = make_str(ctx->wk, path);

				if (!obj_array_in(ctx->wk, ctx->info->libs, str)) {
					L("library '%s' found for dependency '%s'", path, get_cstr(ctx->wk, ctx->name));

					obj_array_push(ctx->wk, ctx->info->libs, str);
				}
			} else {
				LOG_W("library '%s' not found for dependency '%s'", frag->data, get_cstr(ctx->wk, ctx->name));
				obj_array_push(ctx->wk, ctx->info->not_found_libs, make_str(ctx->wk, frag->data));
			}
			break;
		}
		default:
			if (frag->type) {
				obj_array_push(ctx->wk, ctx->info->compile_args,
					make_strf(ctx->wk, "-%c%s", frag->type, frag->data));
			} else {
				L("skipping null pkgconf fragment: '%s'", frag->data);
			}
			break;
		}
	}

ret:
	pkgconf_fragment_free(&list);
	return ret;

}

static bool
apply_modversion(pkgconf_client_t *client, pkgconf_pkg_t *world, void *_ctx, int maxdepth)
{
	struct pkgconf_lookup_ctx *ctx = _ctx;
	pkgconf_dependency_t *dep = world->required.head->data;
	pkgconf_pkg_t *pkg = dep->match;

	if (pkg != NULL && pkg->version != NULL) {
		strncpy(ctx->info->version, pkg->version, MAX_VERSION_LEN);
	}

	return true;
}

bool
muon_pkgconf_lookup(struct workspace *wk, obj name, bool is_static, struct pkgconf_info *info)
{
	if (!init) {
		muon_pkgconf_init(wk);
	}

	int flags = 0;

	if (is_static) {
		flags |= (PKGCONF_PKG_PKGF_SEARCH_PRIVATE | PKGCONF_PKG_PKGF_MERGE_PRIVATE_FRAGMENTS);
	}

	pkgconf_client_set_flags(&client, flags);

	bool ret = true;
	pkgconf_list_t pkgq = PKGCONF_LIST_INITIALIZER;
	pkgconf_queue_push(&pkgq, get_cstr(wk, name));

	struct pkgconf_lookup_ctx ctx = { .wk = wk, .info = info, .name = name, .is_static = is_static };

	if (!pkgconf_queue_apply(&client, &pkgq, apply_modversion, maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

	make_obj(wk, &info->compile_args, obj_array);
	make_obj(wk, &info->link_args, obj_array);
	make_obj(wk, &info->includes, obj_array);
	make_obj(wk, &info->libs, obj_array);
	make_obj(wk, &info->not_found_libs, obj_array);
	make_obj(wk, &ctx.libdirs, obj_array);

	/* uint32_t i; */
	/* for (i = 0; libdirs[i]; ++i) { */
	/* 	obj_array_push(wk, ctx.libdirs, make_str(wk, libdirs[i])); */
	/* } */

	ctx.apply_func = pkgconf_pkg_libs;
	if (!pkgconf_queue_apply(&client, &pkgq, apply_and_collect, maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

	ctx.apply_func = pkgconf_pkg_cflags;
	if (!pkgconf_queue_apply(&client, &pkgq, apply_and_collect, maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

ret:
	pkgconf_queue_free(&pkgq);
	return ret;
}

struct pkgconf_get_variable_ctx {
	struct workspace *wk;
	const char *var;
	obj *res;
};

static bool
apply_variable(pkgconf_client_t *client, pkgconf_pkg_t *world, void *_ctx, int maxdepth)
{
	struct pkgconf_get_variable_ctx *ctx = _ctx;
	bool found = false;
	const char *var;
	pkgconf_dependency_t *dep = world->required.head->data;
	pkgconf_pkg_t *pkg = dep->match;

	if (pkg != NULL) {
		var = pkgconf_tuple_find(client, &pkg->vars, ctx->var);
		if (var != NULL) {
			*ctx->res = make_str(ctx->wk, var);
			found = true;
		}
	}

	return found;
}

bool
muon_pkgconf_get_variable(struct workspace *wk, const char *pkg_name, const char *var, obj *res)
{
	if (!init) {
		muon_pkgconf_init(wk);
	}

	pkgconf_client_set_flags(&client, PKGCONF_PKG_PKGF_SEARCH_PRIVATE);

	pkgconf_list_t pkgq = PKGCONF_LIST_INITIALIZER;
	pkgconf_queue_push(&pkgq, pkg_name);
	bool ret = true;

	struct pkgconf_get_variable_ctx ctx = { .wk = wk, .res = res, .var = var, };

	if (!pkgconf_queue_apply(&client, &pkgq, apply_variable, maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

ret:
	pkgconf_queue_free(&pkgq);
	return ret;
}

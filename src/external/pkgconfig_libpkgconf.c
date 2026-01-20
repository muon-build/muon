/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Ariadne Conill <ariadne@dereferenced.org>
 * SPDX-FileCopyrightText: Masayuki Yamamoto <ma3yuki.8mamo10@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <libpkgconf/libpkgconf.h>
#include <stdlib.h>
#include <string.h>

#include "external/pkgconfig.h"
#include "lang/object.h"
#include "lang/object_iterators.h"
#include "lang/workspace.h"
#include "log.h"
#include "options.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "tracy.h"

static const uint32_t libpkgconf_maxdepth = 256;

struct pkgconf_client {
	pkgconf_client_t client;
	pkgconf_cross_personality_t *personality;
};

static bool
error_handler(const char *msg, const pkgconf_client_t *client, void *data)
{
	LL("libpkgconf: %s", msg);
	return true;
}

static bool
muon_pkgconf_init(struct workspace *wk, struct pkgconf_client *c, enum machine_kind for_machine)
{
	TracyCZoneAutoS;
	c->personality = pkgconf_cross_personality_default();
	pkgconf_client_init(&c->client, error_handler, NULL, c->personality);

	struct obj_array *pkg_config_path;
	{
		obj opt;
		get_option_value_for_machine_overridable(wk, current_project(wk), 0, "pkg_config_path", for_machine, &opt);
		pkg_config_path = get_obj_array(wk, opt);
	}

#ifdef MUON_STATIC
	if (!pkg_config_path->len) {
		LOG_E("Unable to determine pkgconf search path.  Please set "
		      "PKG_CONFIG_PATH or -Dpkg_config_path to an appropriate value.");
		return false;
	}
#endif

	if (pkg_config_path->len) {
		obj v;
		obj_array_for_array(wk, pkg_config_path, v) {
			pkgconf_path_add(get_cstr(wk, v), &c->client.dir_list, true);
		}
	} else {
		// pkgconf_client_dir_list_build uses PKG_CONFIG_PATH and
		// PKG_CONFIG_LIBDIR from the environment, as well as the
		// builtin path (personality->dir_list).  We currently
		// intercept PKG_CONFIG_PATH and turn it into an option, so the
		// above branch should always be taken if PKG_CONFIG_PATH is
		// set.
		pkgconf_client_dir_list_build(&c->client, c->personality);
	}

	TracyCZoneAutoE;
	return true;
}

static void
muon_pkgconf_deinit(struct pkgconf_client *c)
{
	TracyCZoneAutoS;
	pkgconf_cross_personality_deinit(c->personality);
	pkgconf_client_deinit(&c->client);
	TracyCZoneAutoE;
}

static const char *
pkgconf_strerr(int err)
{
	switch (err) {
	case PKGCONF_PKG_ERRF_OK: return "ok";
	case PKGCONF_PKG_ERRF_PACKAGE_NOT_FOUND: return "not found";
	case PKGCONF_PKG_ERRF_PACKAGE_VER_MISMATCH: return "ver mismatch";
	case PKGCONF_PKG_ERRF_PACKAGE_CONFLICT: return "package conflict";
	case PKGCONF_PKG_ERRF_DEPGRAPH_BREAK: return "depgraph break";
	}

	return "unknown";
}

typedef unsigned int (*apply_func)(pkgconf_client_t *client, pkgconf_pkg_t *world, pkgconf_list_t *list, int maxdepth);

struct pkgconf_lookup_ctx {
	apply_func apply_func;
	struct workspace *wk;
	struct pkgconfig_info *info;
	enum muon_pkgconfig_fragment_source frag_source;
};

static bool
apply_and_collect(pkgconf_client_t *client, pkgconf_pkg_t *world, void *_ctx, int maxdepth)
{
	TracyCZoneAutoS;
	struct pkgconf_lookup_ctx *ctx = _ctx;
	int err;
	pkgconf_node_t *node;
	pkgconf_list_t list = PKGCONF_LIST_INITIALIZER;
	bool ret = true;

	err = ctx->apply_func(client, world, &list, maxdepth);
	if (err != PKGCONF_PKG_ERRF_OK) {
		LOG_E("apply_func failed: %s", pkgconf_strerr(err));
		ret = false;
		goto ret;
	}

	PKGCONF_FOREACH_LIST_ENTRY(list.head, node)
	{
		const pkgconf_fragment_t *frag = node->data;

		/* L("got option: -'%c' '%s'", frag->type, frag->data); */

		if (frag->type == 'I' && pkgconf_fragment_has_system_dir(client, frag)) {
			continue;
		}

		struct muon_pkgconfig_fragment muon_frag = {
			.type = frag->type,
			.data = make_str(ctx->wk, frag->data),
		};
		obj default_dest = ctx->frag_source == muon_pkgconfig_fragment_source_cflags ? ctx->info->compile_args :
											       ctx->info->link_args;
		muon_pkgconfig_parse_fragment(ctx->wk, &muon_frag, ctx->info, default_dest);
	}

ret:
	pkgconf_fragment_free(&list);
	TracyCZoneAutoE;
	return ret;
}

static bool
apply_modversion(pkgconf_client_t *client, pkgconf_pkg_t *world, void *_ctx, int maxdepth)
{
	TracyCZoneAutoS;
	struct pkgconf_lookup_ctx *ctx = _ctx;
	pkgconf_dependency_t *dep = world->required.head->data;
	pkgconf_pkg_t *pkg = dep->match;

	if (pkg != NULL && pkg->version != NULL) {
		strncpy(ctx->info->version, pkg->version, MAX_VERSION_LEN);
	}

	TracyCZoneAutoE;
	return true;
}

static bool
pkgconfig_libpkgconf_lookup(struct workspace *wk, struct pkgconfig_info *info)
{
	TracyCZoneAutoS;
	L("libpkgconf: looking up %s %s", get_cstr(wk, info->name), info->is_static ? "static" : "dynamic");
	struct pkgconf_client c = { 0 };
	if (!muon_pkgconf_init(wk, &c, info->for_machine)) {
		return false;
	}

	int flags = 0;

#ifdef _WIN32
	flags |= PKGCONF_PKG_PKGF_REDEFINE_PREFIX;
#endif

	if (info->is_static) {
		flags |= (PKGCONF_PKG_PKGF_SEARCH_PRIVATE | PKGCONF_PKG_PKGF_MERGE_PRIVATE_FRAGMENTS);
	}

	pkgconf_client_set_flags(&c.client, flags);

	bool ret = true;
	pkgconf_list_t pkgq = PKGCONF_LIST_INITIALIZER;
	pkgconf_queue_push(&pkgq, get_cstr(wk, info->name));

	struct pkgconf_lookup_ctx ctx = { .wk = wk, .info = info, /*.name = name, .is_static = is_static, .compiler = compiler,*/ };

	if (!pkgconf_queue_apply(&c.client, &pkgq, apply_modversion, libpkgconf_maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

	ctx.frag_source = muon_pkgconfig_fragment_source_libs;
	ctx.apply_func = pkgconf_pkg_libs;
	if (!pkgconf_queue_apply(&c.client, &pkgq, apply_and_collect, libpkgconf_maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

	// meson runs pkg-config to look for cflags,
	// which honors Requires.private if any cflags are requested.
	pkgconf_client_set_flags(&c.client, flags | PKGCONF_PKG_PKGF_SEARCH_PRIVATE);

	ctx.frag_source = muon_pkgconfig_fragment_source_cflags;
	ctx.apply_func = pkgconf_pkg_cflags;
	if (!pkgconf_queue_apply(&c.client, &pkgq, apply_and_collect, libpkgconf_maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

	pkgconf_client_set_flags(&c.client, flags);

ret:
	pkgconf_queue_free(&pkgq);
	muon_pkgconf_deinit(&c);
	TracyCZoneAutoE;
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

static bool
pkgconfig_libpkgconf_get_variable(struct workspace *wk, obj pkg_name, obj var_name, obj defines, enum machine_kind m, obj *res)
{
	struct pkgconf_client c = { 0 };
	if (!muon_pkgconf_init(wk, &c, m)) {
		return false;
	}

	pkgconf_client_set_flags(&c.client, PKGCONF_PKG_PKGF_SEARCH_PRIVATE);

	if (defines) {
		obj k, v;
		obj_dict_for(wk, defines, k, v) {
			pkgconf_tuple_add(&c.client,
				&c.client.global_vars,
				get_cstr(wk, k),
				get_cstr(wk, v),
				false,
				PKGCONF_PKG_TUPLEF_OVERRIDE);
		}
	}

	pkgconf_list_t pkgq = PKGCONF_LIST_INITIALIZER;
	pkgconf_queue_push(&pkgq, get_cstr(wk, pkg_name));
	bool ret = true;

	struct pkgconf_get_variable_ctx ctx = {
		.wk = wk,
		.res = res,
		.var = get_cstr(wk, var_name),
	};

	if (!pkgconf_queue_apply(&c.client, &pkgq, apply_variable, libpkgconf_maxdepth, &ctx)) {
		ret = false;
		goto ret;
	}

ret:
	pkgconf_queue_free(&pkgq);
	muon_pkgconf_deinit(&c);
	return ret;
}

const struct pkgconfig_impl pkgconfig_impl_libpkgconf = {
	.lookup = pkgconfig_libpkgconf_lookup,
	.get_variable = pkgconfig_libpkgconf_get_variable,
};

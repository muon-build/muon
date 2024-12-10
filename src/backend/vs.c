/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdlib.h>

#include <windows.h>

#include "compat.h"

#include "args.h"
#include "backend/backend.h"
#include "backend/output.h"
#include "backend/vs.h"
#include "backend/vs/filter.h"
#include "backend/vs/project.h"
#include "backend/vs/solution.h"
#include "error.h"

char *vs_configurations[2] = { "Debug", "Release" };

char *vs_platforms[2] = { "x86", "x64" };

char *vs_machines[2] = { "Win32", "x64" };

static enum iteration_result
vs_target_dep_int_iter(struct workspace *wk, void *_ctx, obj dep)
{
	LOG_E(" ### %s", "vs_target_dep_int_iter");
	switch (get_obj_type(wk, dep)) {
	case obj_string:
		LOG_E("    dep int (string): '%s'", get_cstr(wk, dep));
		break;
	case obj_file:
		LOG_E("    dep int (file)  : '%s'", get_file_path(wk, dep));
		break;
	case obj_dependency:
	{
		struct obj_dependency *depr = get_obj_dependency(wk, dep);
		LOG_E("    dep int (dep)  : '%s'", get_cstr(wk, depr->name));
		break;
	}
	default:
		LOG_E("    dep int (other) : '%d'", get_obj_type(wk, dep));
		break;//UNREACHABLE;
	}

	return ir_cont;
}

static enum iteration_result
check_vs_linkdep_iter(struct workspace *wk, void *_ctx, obj link_depend)
{
	enum obj_type t = get_obj_type(wk, link_depend);

	printf("oui !\n");
	fflush(stdout);
	/* return ir_cont; */

	switch (t) {
	case obj_string:
		printf(" str: '%s'\n", get_cstr(wk, link_depend));
		break;
	case obj_file:
		printf(" file:\n");
		break;
	default: UNREACHABLE;
	}

	return ir_cont;
}

static bool
vs_detect(struct workspace *wk, enum backend_output *backend)
{
	char *version;

	if (!(version = getenv("VisualStudioVersion"))) {
		return false;
	}

	bool ret = false;
	if (strcmp(version, "16.0") == 0) {
		*backend = backend_output_vs2019;
		ret = true;
	} else if (strcmp(version, "17.0") == 0) {
		*backend = backend_output_vs2022;
		ret = true;
	}

	return ret;
}

static enum iteration_result
vs_target_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, tgt_id);
	const char *name = NULL;

	ctx->tgt_id = tgt_id;

	switch (t) {
	case obj_alias_target: {
		name = get_cstr(wk, get_obj_alias_target(wk, tgt_id)->name);
		LOG_E(" *** alias_target");
		break;
	}
	case obj_both_libs: {
		tgt_id = get_obj_both_libs(wk, tgt_id)->dynamic_lib;
		LOG_E(" *** both_libs");
	}
	/* fallthrough */
	case obj_build_target: {
		LOG_E(" *** build_target");

		struct obj_build_target *target = get_obj_build_target(wk, tgt_id);
		LOG_E("name        : '%s'", get_cstr(wk, target->name));
		LOG_E("build_name  : '%s'", get_cstr(wk, target->build_name));
		LOG_E("build_path  : '%s'", get_cstr(wk, target->build_path));
		LOG_E("private_path: '%s'", get_cstr(wk, target->private_path));
		LOG_E("cwd         : '%s'", get_cstr(wk, target->cwd));
		LOG_E("build_dir   : '%s'", get_cstr(wk, target->build_dir));
		LOG_E("soname      : '%s'", get_cstr(wk, target->soname));
		LOG_E("type        : '%d'", target->type);
		LOG_E("link_depends: '%d'", target->link_depends);
		LOG_E("dep_internal: '%d'", target->dep_internal.link_with);

		if (target->dep_internal.link_with) {
			obj_array_foreach(wk, target->dep_internal.link_with, NULL, vs_target_dep_int_iter);
		} else {
			LOG_E("   no int dep !!");
		}

		name = get_cstr(wk, get_obj_build_target(wk, tgt_id)->build_name);

		if (target->link_depends) {
			obj_array_foreach(wk, get_obj_build_target(wk, tgt_id)->link_depends, NULL, check_vs_linkdep_iter);
		} else {
			printf("ZUT\n");
			fflush(stdout);
		}

		SBUF_manual(project_name);
		vs_get_project_filename(wk, &project_name, get_obj_build_target(wk, tgt_id));
		printf(" **** project filename: '%s'\n", project_name.buf);
		bool ret = with_open(wk->build_root, project_name.buf, wk, _ctx, vs_write_project);
		if (!ret) {
			sbuf_destroy(&project_name);
			return ir_err;
		}

		sbuf_pushs(0, &project_name, ".filters");
		ret = with_open(wk->build_root, project_name.buf, wk, _ctx, vs_write_filter);
		sbuf_destroy(&project_name);
		if (!ret)
			return ir_err;
		break;
	}
	case obj_custom_target: {
		LOG_E(" *** custom_target");
		name = get_cstr(wk, get_obj_custom_target(wk, tgt_id)->name);
		break;
	}
	default: UNREACHABLE;
	}

	return ir_cont;
}

void
vs_get_project_filename(struct workspace *wk, struct sbuf *sb, struct obj_build_target *target)
{
	sbuf_pushs(0, sb, get_cstr(wk, target->name));
	switch (target->type) {
	case tgt_executable:
		sbuf_pushs(0, sb, "@exe.vcxproj");
		break;
	case tgt_static_library:
		sbuf_pushs(0, sb, "@sta.vcxproj");
		break;
	case tgt_dynamic_library:
		sbuf_pushs(0, sb, "@sha.vcxproj");
		break;
	case tgt_shared_module:
		sbuf_pushs(0, sb, "@mod.vcxproj");
		break;
	}
}


bool
vs_write_all(struct workspace *wk, enum backend_output backend)
{
	struct vs_ctx ctx;

	if (backend == backend_output_vs) {
		if (!vs_detect(wk, &backend)) {
			LOG_E("Could not detect Visual Studio."
			      "Are you running muon from the "
			      "Visual Studio Developer Command Prompt ?");
			return false;
		}
	}

	switch (backend) {
	case backend_output_vs2019: ctx.vs_version = 16; break;
	case backend_output_vs2022: ctx.vs_version = 17; break;
	default:
		LOG_E("Unsupported Visual Studio version.");
		return false;
	}

	// FIXME: also other (sub)projects ?
	ctx.project = arr_get(&wk->projects, 0);

	SBUF_manual(sln_name);
	sbuf_pushs(0, &sln_name, get_cstr(wk, ctx.project->cfg.name));
	sbuf_pushs(0, &sln_name, ".sln");
	bool ret = with_open(wk->build_root, sln_name.buf, wk, &ctx, vs_write_solution);
	sbuf_destroy(&sln_name);
	if (!ret) return false;

	ctx.idx = 0;
	obj_array_foreach(wk, ctx.project->targets, &ctx, vs_target_iter);

	return true;
}

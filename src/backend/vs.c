/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdlib.h>

#include <windows.h>
#include <rpc.h>

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

#if 0

struct vs_sln_projects {
	obj filename; /* string */
	uint64_t guid;
};

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

static enum iteration_result
vs_target_src_iter(struct workspace *wk, void *_ctx, obj src)
{
	switch (get_obj_type(wk, src)) {
	case obj_string:
		printf("  src (string): '%s'\n", get_cstr(wk, src));
		break;
	case obj_file:
		printf("  src (file)  : '%s'\n", get_file_path(wk, src));
		break;
	default:
		UNREACHABLE;
	}

	return ir_cont;
}

static enum iteration_result
vs_target_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_sln_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, tgt_id);
	const char *name = NULL;
	switch (t) {
	case obj_alias_target: name = get_cstr(wk, get_obj_alias_target(wk, tgt_id)->name); break;
	case obj_both_libs: tgt_id = get_obj_both_libs(wk, tgt_id)->dynamic_lib;
	/* fallthrough */
	case obj_build_target: {
		{
			struct obj_build_target *target = get_obj_build_target(wk, tgt_id);
			LOG_E("name        : '%s'", get_cstr(wk, target->name));
			LOG_E("build_name  : '%s'", get_cstr(wk, target->build_name));
			LOG_E("build_path  : '%s'", get_cstr(wk, target->build_path));
			LOG_E("private_path: '%s'", get_cstr(wk, target->private_path));
			LOG_E("cwd         : '%s'", get_cstr(wk, target->cwd));
			LOG_E("build_dir   : '%s'", get_cstr(wk, target->build_dir));
			LOG_E("soname      : '%s'", get_cstr(wk, target->soname));
			if (target->src) {
				obj_array_foreach(wk, target->src, NULL, vs_target_src_iter);
			} else {
				LOG_E("   no source !!");
			}
			if (target->dep_internal.link_with) {
				obj_array_foreach(wk, target->dep_internal.link_with, NULL, vs_target_dep_iter);
			} else {
				LOG_E("   no dep !!");
			}
		}
		name = get_cstr(wk, get_obj_build_target(wk, tgt_id)->build_name);
		if (get_obj_build_target(wk, tgt_id)->link_depends) {
			obj_array_foreach(wk, get_obj_build_target(wk, tgt_id)->link_depends, NULL, check_vs_linkdep_iter);
		} else {
			printf("ZUT\n");
			fflush(stdout);
		}
		break;
	}
	case obj_custom_target: name = get_cstr(wk, get_obj_custom_target(wk, tgt_id)->name); break;
	default: UNREACHABLE;
	}

	LOG_E("  tgt '%s'", name);

	return ir_cont;
}

static bool
vs_write_solution(struct workspace *wk, void *_ctx, FILE *out)
{
	struct vs_sln_ctx *ctx = _ctx;

	/* header */
	fprintf(out,
		"Microsoft Visual Studio Solution File, Format Version 12.00\n"
		"# Visual Studio Version %d\n",
		ctx->vs_sln_version);

	for (uint32_t i = 0; i < wk->projects.len; ++i) {
		struct project *proj = arr_get(&wk->projects, i);
		struct obj_array *targets = get_obj_array(wk, proj->targets);

		obj_array_foreach(wk, proj->targets, NULL, vs_target_iter);
		LOG_E(" project #%d: '%s' (type: %d %d)", i, get_cstr(wk, proj->cfg.name), get_obj_type(wk, proj->targets), get_obj_array(wk, proj->targets)->len);
		/* header */
#if 0
		for (uint32_t i = 0; i < targets->len; i++) {
			SBUF_manual(guid_project_str);
			sbuf_pushf(0, &guid_project_str,
				   "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
				   targets_guids[i].data1,
				   targets_guids[i].data2,
				   targets_guids[i].data3,
				   targets_guids[i].data4[0],
				   targets_guids[i].data4[1],
				   targets_guids[i].data4[2],
				   targets_guids[i].data4[3],
				   targets_guids[i].data4[4],
				   targets_guids[i].data4[5],
				   targets_guids[i].data4[6],
				   targets_guids[i].data4[7]);

			LOG_E("  guid: %s\n", guid_project_str.buf);
			fprintf(out,
				"Project(\"{%s}\") = \n",
				vs_sln_guid[VS_SLN_GUID_LANG_C]);
		}
#endif
	}

	return true;
}

static enum iteration_result
vs_sln_header_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_sln_ctx *ctx = _ctx;
	const char *vs_project_name = NULL;
	enum obj_type t = get_obj_type(wk, tgt_id);

	switch (t) {
	case obj_both_libs: tgt_id = get_obj_both_libs(wk, tgt_id)->dynamic_lib;
	/* fallthrough */
	case obj_build_target: vs_project_name = get_cstr(wk, get_obj_build_target(wk, tgt_id)->name); break;
	case obj_custom_target: vs_project_name = get_cstr(wk, get_obj_custom_target(wk, tgt_id)->name); break;
	default: UNREACHABLE;
	}

	if (!vs_project_name) return ir_err;

	struct guid *guid_project = arr_get(&ctx->projects_guid, ctx->idx);
	SBUF_manual(guid_project_str);
	sbuf_pushf(0, &guid_project_str,
		   "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		   guid_project->data1,
		   guid_project->data2,
		   guid_project->data3,
		   guid_project->data4[0],
		   guid_project->data4[1],
		   guid_project->data4[2],
		   guid_project->data4[3],
		   guid_project->data4[4],
		   guid_project->data4[5],
		   guid_project->data4[6],
		   guid_project->data4[7]);

	fprintf(ctx->out,
		"Project(\"{%s}\") = \"%s\", \"%s.vcxproj\", \"{%s}\"\n",
		vs_sln_guid[VS_SLN_GUID_LANG_C],
		vs_project_name,
		vs_project_name,
		guid_project_str.buf);
	sbuf_destroy(&guid_project_str);

	// FIXME add dependencies

	fprintf(ctx->out, "EndProject\n");

	ctx->idx++;
	return ir_cont;
}

static enum iteration_result
vs_sln_body_iter(struct workspace *wk, void *_ctx, obj tgt_id)


#endif

static enum iteration_result
vs_target_dep_iter(struct workspace *wk, void *_ctx, obj dep)
{
	switch (get_obj_type(wk, dep)) {
	case obj_string:
		printf("  dep (string): '%s'\n", get_cstr(wk, dep));
		break;
	case obj_file:
		printf("  dep (file)  : '%s'\n", get_file_path(wk, dep));
		break;
	case obj_dependency:
	{
		struct obj_dependency *depr = get_obj_dependency(wk, dep);
		printf("  dep (name)  : '%s'\n", get_cstr(wk, depr->name));
		break;
	}
	default:
		printf("  dep (other) : '%d'\n", get_obj_type(wk, dep));
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
vs_guid_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_ctx *ctx = _ctx;
	struct guid guid;
	GUID g;
	UuidCreate(&g);
	memcpy(&guid, &g, sizeof(struct guid));
	arr_push(&ctx->projects_guid, &guid);

	return ir_cont;
}

static enum iteration_result cb_dict(struct workspace *wk, void *ctx, obj key, obj val)
{
	enum obj_type t1 = get_obj_type(wk, key);
	enum obj_type t2 = get_obj_type(wk, val);
	printf(" ** dict : %d %d\n", t1, t2);
	fflush(stdout);
	return ir_cont;
}

static enum iteration_result cb_dict2(struct workspace *wk, void *ctx, obj key, obj val)
{
	enum obj_type t1 = get_obj_type(wk, key);
	enum obj_type t2 = get_obj_type(wk, val);
	printf(" ** dict2 : %d %d\n", t1, t2);
	fflush(stdout);
	return ir_cont;
}

static enum iteration_result
vs_target_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, tgt_id);
	const char *name = NULL;

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
		ctx->target = target;
		LOG_E("name        : '%s'", get_cstr(wk, target->name));
		LOG_E("build_name  : '%s'", get_cstr(wk, target->build_name));
		LOG_E("build_path  : '%s'", get_cstr(wk, target->build_path));
		LOG_E("private_path: '%s'", get_cstr(wk, target->private_path));
		LOG_E("cwd         : '%s'", get_cstr(wk, target->cwd));
		LOG_E("build_dir   : '%s'", get_cstr(wk, target->build_dir));
		LOG_E("soname      : '%s'", get_cstr(wk, target->soname));
		LOG_E("type        : '%d'", target->type);

		if (target->args) {
			printf(" ** dict: args \n");
			obj_dict_foreach(wk, target->args, NULL, cb_dict);
		} else {
			printf(" ** dict: pas d'args\n");
		}
		if (target->required_compilers) {
			printf(" ** dict2: required_compilers \n");
			obj_dict_foreach(wk, target->required_compilers, NULL, cb_dict2);
		} else {
			printf(" ** dict2: pas d'required_compilers\n");
		}
		/* if (target->src) { */
		/* 	obj_array_foreach(wk, target->src, NULL, vs_target_src_iter); */
		/* } else { */
		/* 	LOG_E("   no source !!"); */
		/* } */

		if (target->dep_internal.link_with) {
			obj_array_foreach(wk, target->dep_internal.link_with, NULL, vs_target_dep_iter);
		} else {
			LOG_E("   no dep !!");
		}

		name = get_cstr(wk, get_obj_build_target(wk, tgt_id)->build_name);

		if (target->link_depends) {
			obj_array_foreach(wk, get_obj_build_target(wk, tgt_id)->link_depends, NULL, check_vs_linkdep_iter);
		} else {
			printf("ZUT\n");
			fflush(stdout);
		}

		obj tgt_args;
		if (!arr_to_args(wk, 0, target->args, &tgt_args)) {
			UNREACHABLE;
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

	arr_init(&ctx.projects_guid, 2, sizeof(struct guid));
	obj_array_foreach(wk, ctx.project->targets, &ctx, vs_guid_iter);

	for (uint32_t i = 0; i < ctx.projects_guid.len; i++)
	{

		struct guid *g = arr_get(&ctx.projects_guid, i);
		printf(" 1 : %u\n", g->data1);
	}

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

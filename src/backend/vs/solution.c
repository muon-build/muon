/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "backend/backend.h"
#include "backend/vs.h"
#include "error.h"

/* https://learn.microsoft.com/en-us/visualstudio/extensibility/internals/solution-dot-sln-file?view=vs-2022 */

enum vs_sln_guid {
	VS_SLN_GUID_FOLDER,
	VS_SLN_GUID_LANG_C,
	VS_SLN_GUID_LANG_CPP,
	VS_SLN_GUID_LAST,
};

/* same order than enum vs_sln_guid above ! */
/* https://github.com/JamesW75/visual-studio-project-type-guid */
static const char *vs_sln_guid[VS_SLN_GUID_LAST] = {
	"2150E333-8FDC-42A3-9474-1A3956D46DE8",
	"8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942",
	"8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942"
};

struct sln_target_dep_iter_ctx
{
	FILE *out;
	obj name;
};

struct sln_target_iter_ctx
{
	FILE *out;
	const struct project *project;
};

static enum iteration_result
vs_sln_dep_find_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct sln_target_dep_iter_ctx *ctx = _ctx;
	struct obj_build_target *target = get_obj_build_target(wk, tgt_id);
	printf(" ###### : cur name : %u '%s'\n", ctx->name, get_cstr(wk, ctx->name));
	printf(" ###### : dep tgt '%u'\n", tgt_id);
	printf(" ###### : tgt name : %u '%s'\n",
	       target->build_path,
	       get_cstr(wk, target->build_path));
	fflush(stdout);
	if (strcmp(get_cstr(wk, ctx->name), get_cstr(wk, target->build_path)) == 0) {
		fprintf(ctx->out, "\t\t{%04X} = {%04X}\n", tgt_id, tgt_id);
	}

	return ir_cont;
}

static enum iteration_result
vs_sln_dep_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct sln_target_iter_ctx *ctx = _ctx;
	switch (get_obj_type(wk, val)) {
	case obj_dependency:
	{
		struct obj_dependency *dep = get_obj_dependency(wk, val);
		struct sln_target_dep_iter_ctx ctx_dep;
		ctx_dep.out = ctx->out;
		ctx_dep.name = dep->name;
		LOG_E("    dep (dep)  : '%u' (type: %d)  '%s'", val, get_obj_type(wk, dep->name), get_cstr(wk, dep->name));
		obj_array_foreach(wk, ctx->project->targets, &ctx_dep, vs_sln_dep_find_iter);
		break;
	}
	default:
		LOG_E("    dep (other) : '%d'", get_obj_type(wk, val));
		break;//UNREACHABLE;
	}

	return ir_cont;
}

static enum iteration_result
vs_sln_header_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_ctx *ctx = _ctx;
	struct obj_build_target *target = get_obj_build_target(wk, tgt_id);

	// FIXME: check langage ?
	SBUF_manual(project_name);
	vs_get_project_filename(wk, &project_name, get_obj_build_target(wk, tgt_id));
	fprintf(ctx->out,
		"Project(\"{%s}\") = \"%s\", \"%s\", \"{%04X}\"\n",
		vs_sln_guid[VS_SLN_GUID_LANG_C],
		get_cstr(wk, target->name),
		project_name.buf,
		tgt_id);
	sbuf_destroy(&project_name);

	// add dependencies
	if (target->dep_internal.raw.deps) {
		struct sln_target_iter_ctx ctx_tgt;

		ctx_tgt.out = ctx->out;
		ctx_tgt.project = ctx->project;
		fprintf(ctx->out, "\tProjectSection(ProjectDependencies) = postProject\n");
		obj_array_foreach(wk, target->dep_internal.raw.deps, &ctx_tgt, vs_sln_dep_iter);
		fprintf(ctx->out, "\tEndProjectSection\n");
	}

	fprintf(ctx->out, "EndProject\n");

	ctx->idx++;
	return ir_cont;
}

static enum iteration_result
vs_sln_body_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_ctx *ctx = _ctx;
	for (uint32_t i = 0; i < ARRAY_LEN(vs_configurations); i++) {
		for (uint32_t j = 0; j < ARRAY_LEN(vs_platforms); j++) {
			fprintf(ctx->out, "\t\t{%04X}.%s|%s.ActiveCfg = %s|%s\n",
				tgt_id,
				vs_configurations[i],
				vs_machines[j],
				vs_configurations[i],
				vs_machines[j]);
			fprintf(ctx->out, "\t\t{%04X}.%s|%s.Build.0 = %s|%s\n",
				tgt_id,
				vs_configurations[i],
				vs_machines[j],
				vs_configurations[i],
				vs_machines[j]);
		}
	}

	ctx->idx++;
	return ir_cont;
}

bool
vs_write_solution(struct workspace *wk, void *_ctx, FILE *out)
{
	struct vs_ctx *ctx = _ctx;

	ctx->out = out;

	/* header */
	fprintf(out,
		"Microsoft Visual Studio Solution File, Format Version 12.00\n"
		"# Visual Studio Version %d\n",
		ctx->vs_version);

	ctx->idx = 0;
	obj_array_foreach(wk, ctx->project->targets, _ctx, vs_sln_header_iter);

	/* body */
	fprintf(out, "Global\n");

	fprintf(out, "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n");
	for (uint32_t i = 0; i < ARRAY_LEN(vs_configurations); i++) {
		for (uint32_t j = 0; j < ARRAY_LEN(vs_platforms); j++) {
			fprintf(out, "\t\t%s|%s = %s|%s\n",
				vs_configurations[i],
				vs_platforms[j],
				vs_configurations[i],
				vs_platforms[j]);
		}
	}
	fprintf(out, "\tEndGlobalSection\n");

	fprintf(out, "\tGlobalSection(SolutionConfigurationPlatforms) = postSolution\n");
	ctx->idx = 0;
	obj_array_foreach(wk, ctx->project->targets, _ctx, vs_sln_body_iter);
	fprintf(out, "\tEndGlobalSection\n");

	fprintf(out, "EndGlobal\n");

	return true;
}

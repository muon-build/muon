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

static enum iteration_result
vs_sln_header_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_ctx *ctx = _ctx;
	struct obj_build_target *target = get_obj_build_target(wk, tgt_id);

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

	// FIXME: check langage ?
	SBUF_manual(project_name);
	vs_get_project_filename(wk, &project_name, get_obj_build_target(wk, tgt_id));
	fprintf(ctx->out,
		"Project(\"{%s}\") = \"%s\", \"%s\", \"{%s}\"\n",
		vs_sln_guid[VS_SLN_GUID_LANG_C],
		get_cstr(wk, target->name),
		project_name.buf,
		guid_project_str.buf);
	sbuf_destroy(&project_name);
	sbuf_destroy(&guid_project_str);

	// FIXME add dependencies

	fprintf(ctx->out, "EndProject\n");

	ctx->idx++;
	return ir_cont;
}

static enum iteration_result
vs_sln_body_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_ctx *ctx = _ctx;
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
	for (uint32_t i = 0; i < ARRAY_LEN(vs_configurations); i++) {
		for (uint32_t j = 0; j < ARRAY_LEN(vs_platforms); j++) {
			fprintf(ctx->out, "\t\t{%s}.%s|%s.ActiveCfg = %s|%s\n",
				guid_project_str.buf,
				vs_configurations[i],
				vs_machines[j],
				vs_configurations[i],
				vs_machines[j]);
			fprintf(ctx->out, "\t\t{%s}.%s|%s.Build.0 = %s|%s\n",
				guid_project_str.buf,
				vs_configurations[i],
				vs_machines[j],
				vs_configurations[i],
				vs_machines[j]);
		}
	}

	sbuf_destroy(&guid_project_str);

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

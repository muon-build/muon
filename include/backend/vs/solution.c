/*
 * SPDX-FileCopyrightText: Vincent Torri <vincent.torri@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "vs.h"

static enum iteration_result
vs_sln_header_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct vs_ctx *ctx = _ctx;
	const char *vs_project_name = NULL;
	enum obj_type t = get_obj_type(wk, tgt_id);
	printf(" * %u\n", ctx->idx);

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
{
	struct vs_sln_ctx *ctx = _ctx;
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
	for (uint32_t i = 0; i < 4; i++) {
		fprintf(ctx->out, "\t\t{%s}.%s.ActiveCfg = %s\n",
			guid_project_str.buf,
			vs_sln_cfg_platform[i][0],
			vs_sln_cfg_platform[i][1]);
		fprintf(ctx->out, "\t\t{%s}.%s.Build.0 = %s\n",
			guid_project_str.buf,
			vs_sln_cfg_platform[i][0],
			vs_sln_cfg_platform[i][1]);
	}

	sbuf_destroy(&guid_project_str);

	ctx->idx++;
	return ir_cont;
}

bool
vs_write_solution2(struct workspace *wk, void *_ctx, FILE *out)
{
	struct vs_ctx *ctx = _ctx;

	ctx->out = out;

	/* header */
	fprintf(out,
		"Microsoft Visual Studio Solution File, Format Version 12.00\n"
		"# Visual Studio Version %d\n",
		ctx->vs_sln_version);

	ctx->idx = 0;
	obj_array_foreach(wk, ctx->project->targets, _ctx, vs_sln_header_iter);

	/* body */
	fprintf(out, "Global\n");

	fprintf(out, "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n");
	for (uint32_t i = 0; i < 4; i++) {
		fprintf(out, "\t\t%s = %s\n",
			vs_sln_cfg_platform[i][0], vs_sln_cfg_platform[i][0]);
	}
	fprintf(out, "\tEndGlobalSection\n");

	fprintf(out, "\tGlobalSection(SolutionConfigurationPlatforms) = postSolution\n");
	ctx->idx = 0;
	obj_array_foreach(wk, ctx->project->targets, _ctx, vs_sln_body_iter);
	fprintf(out, "\tEndGlobalSection\n");

	fprintf(out, "EndGlobal\n");

	return true;
}

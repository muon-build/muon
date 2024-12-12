/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@thunix.net>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja.h"
#include "backend/ninja/custom_target.h"
#include "lang/serial.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

// appended to custom_target data files to make them unique
static uint32_t custom_tgt_dat_sequence = 0;

static enum iteration_result
ninja_args_are_escapable_iter(struct workspace *wk, void *_ctx, obj v)
{
	const struct str *ss = get_str(wk, v);
	if (str_has_null(ss)) {
		return ir_err;
	}

	if (strchr(ss->s, '\n')) {
		return ir_err;
	}

	return ir_cont;
}

static bool
ninja_args_are_escapable(struct workspace *wk, obj arr)
{
	return obj_array_foreach(wk, arr, NULL, ninja_args_are_escapable_iter);
}

static bool
write_custom_target_dat(struct workspace *wk, struct obj_custom_target *tgt, obj data_obj, const char *dir, obj *res)
{
	assert(tgt->name && "unnamed targets cannot have a custom data");

	SBUF(name);
	sbuf_pushf(wk, &name, "%s%d.dat", get_cstr(wk, tgt->name), custom_tgt_dat_sequence);
	++custom_tgt_dat_sequence;

	uint32_t i;
	for (i = 0; i < name.len; ++i) {
		if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-_", name.buf[i])) {
			name.buf[i] = '_';
		}
	}

	SBUF(dirpath);
	SBUF(dat_path);
	path_join(wk, &dirpath, wk->muon_private, dir);
	path_join(wk, &dat_path, dirpath.buf, name.buf);

	FILE *dat;

	if (!fs_mkdir_p(dirpath.buf)) {
		return false;
	} else if (!(dat = fs_fopen(dat_path.buf, "wb"))) {
		return false;
	} else if (!serial_dump(wk, data_obj, dat)) {
		return false;
	} else if (!fs_fclose(dat)) {
		return false;
	}

	*res = make_str(wk, dat_path.buf);
	return true;
}

bool
ninja_write_custom_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *ctx)
{
	struct obj_custom_target *tgt = get_obj_custom_target(wk, tgt_id);
	L("writing rules for custom target '%s'", get_cstr(wk, tgt->name));

	obj outputs, inputs = 0, cmdline;

	if (tgt->input) {
		ca_relativize_paths(wk, tgt->input, false, &inputs);
	}

	make_obj(wk, &outputs, obj_array);
	if (tgt->output) {
		ca_relativize_paths(wk, tgt->output, false, &outputs);
	} else {
		assert(tgt->name && "unnamed targets cannot have no output");
		obj name;

		if (ctx->proj->subproject_name) {
			name = make_strf(
				wk, "%s@@%s", get_cstr(wk, ctx->proj->subproject_name), get_cstr(wk, tgt->name));
		} else {
			name = tgt->name;
		}

		obj_array_push(wk, outputs, name);
	}

	make_obj(wk, &cmdline, obj_array);
	obj_array_push(wk, cmdline, make_str(wk, wk->argv0));
	obj_array_push(wk, cmdline, make_str(wk, "internal"));
	obj_array_push(wk, cmdline, make_str(wk, "exe"));

	if (tgt->flags & custom_target_capture) {
		obj_array_push(wk, cmdline, make_str(wk, "-c"));

		obj elem;
		obj_array_index(wk, tgt->output, 0, &elem);

		ca_relativize_path_push(wk, elem, cmdline);
	}

	if (tgt->flags & custom_target_feed) {
		obj_array_push(wk, cmdline, make_str(wk, "-f"));

		obj elem;
		obj_array_index(wk, tgt->input, 0, &elem);

		ca_relativize_path_push(wk, elem, cmdline);
	}

	if (tgt->env) {
		obj env_dat_path;
		if (!write_custom_target_dat(wk, tgt, tgt->env, "custom_tgt_env", &env_dat_path)) {
			return ir_err;
		}

		obj_array_push(wk, cmdline, make_str(wk, "-e"));
		obj_array_push(wk, cmdline, env_dat_path);
	}

	obj tgt_args;
	if (!arr_to_args(wk, 0, tgt->args, &tgt_args)) {
		return ir_err;
	}

	if (ninja_args_are_escapable(wk, tgt_args)) {
		obj_array_push(wk, cmdline, make_str(wk, "--"));
		obj_array_extend_nodup(wk, cmdline, tgt_args);
	} else {
		obj args_dat_path;
		if (!write_custom_target_dat(wk, tgt, tgt_args, "custom_tgt_args", &args_dat_path)) {
			return ir_err;
		}

		obj_array_push(wk, cmdline, make_str(wk, "-a"));
		obj_array_push(wk, cmdline, args_dat_path);
	}

	obj depends_rel;
	ca_relativize_paths(wk, tgt->depends, false, &depends_rel);

	if (tgt->flags & custom_target_build_always_stale) {
		obj_array_push(wk, depends_rel, make_str(wk, "build_always_stale"));
	}

	obj depends = join_args_ninja(wk, depends_rel);
	outputs = join_args_ninja(wk, outputs);
	inputs = inputs ? join_args_ninja(wk, inputs) : make_str(wk, "");
	cmdline = join_args_shell_ninja(wk, cmdline);

	const char *rule;
	if (tgt->depfile) {
		rule = "CUSTOM_COMMAND_DEP";
	} else {
		rule = "CUSTOM_COMMAND";
	}

	fprintf(ctx->out,
		"build %s: %s %s | %s\n"
		" COMMAND = %s\n",
		get_cstr(wk, outputs),
		rule,
		get_cstr(wk, inputs),
		get_cstr(wk, depends),
		get_cstr(wk, cmdline));

	if (tgt->depfile) {
		obj depfile_rel;
		ca_relativize_path(wk, tgt->depfile, false, &depfile_rel);
		fprintf(ctx->out, " DEPFILE = %s\n", get_cstr(wk, depfile_rel));
	}

	if (tgt->flags & custom_target_console) {
		fprintf(ctx->out, " pool = console\n");
	}

	if (tgt->flags & custom_target_build_by_default) {
		ctx->wrote_default = true;
		fprintf(ctx->out, "default %s\n", get_cstr(wk, outputs));
	}

	fprintf(ctx->out, "\n");
	return ir_cont;
}

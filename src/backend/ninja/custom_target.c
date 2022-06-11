#include "posix.h"

#include "args.h"
#include "backend/common_args.h"
#include "backend/ninja.h"
#include "backend/ninja/custom_target.h"
#include "lang/serial.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

// appended to custom_target environment files to make them unique
static uint32_t custom_tgt_env_sequence = 0;

bool
ninja_write_custom_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *ctx)
{
	struct obj_custom_target *tgt = get_obj_custom_target(wk, tgt_id);
	LOG_I("writing rules for custom target '%s'", get_cstr(wk, tgt->name));

	obj outputs, inputs = 0, cmdline;

	if (tgt->input) {
		if (!relativize_paths(wk, tgt->input, false, &inputs)) {
			return ir_err;
		}
	}

	make_obj(wk, &outputs, obj_array);
	if (tgt->output) {
		if (!relativize_paths(wk, tgt->output, false, &outputs)) {
			return ir_err;
		}
	} else {
		assert(tgt->name && "unnamed targets cannot have no output");
		obj name;

		if (ctx->proj->subproject_name) {
			name = make_strf(wk, "%s@@%s", get_cstr(wk, ctx->proj->subproject_name), get_cstr(wk, tgt->name));
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

		if (!relativize_path_push(wk, elem, cmdline)) {
			return ir_err;
		}
	}

	if (tgt->flags & custom_target_feed) {
		obj_array_push(wk, cmdline, make_str(wk, "-f"));

		obj elem;
		obj_array_index(wk, tgt->input, 0, &elem);

		if (!relativize_path_push(wk, elem, cmdline)) {
			return ir_err;
		}
	}

	if (tgt->env) {
		assert(tgt->name && "unnamed targets cannot have a custom env");

		char name[PATH_MAX] = { 0 };
		snprintf(name, PATH_MAX - 1, "%s%d.dat", get_cstr(wk, tgt->name), custom_tgt_env_sequence);
		++custom_tgt_env_sequence;

		char env_dat_path[PATH_MAX], dir[PATH_MAX];
		if (!(path_join(dir, PATH_MAX, wk->muon_private, "custom_tgt_env")
		      && path_join(env_dat_path, PATH_MAX, dir, name)
		      )) {
			return false;
		}

		FILE *env_dat;

		if (!fs_mkdir_p(dir)) {
			return false;
		} else if (!(env_dat = fs_fopen(env_dat_path, "w"))) {
			return false;
		} else if (!serial_dump(wk, tgt->env, env_dat)) {
			return false;
		} else if (!fs_fclose(env_dat)) {
			return false;
		}

		obj_array_push(wk, cmdline, make_str(wk, "-e"));
		obj_array_push(wk, cmdline, make_str(wk, env_dat_path));
	}

	obj_array_push(wk, cmdline, make_str(wk, "--"));

	obj tgt_args;
	if (!arr_to_args(wk, 0, tgt->args, &tgt_args)) {
		return ir_err;
	}

	obj depends_rel;
	if (!relativize_paths(wk, tgt->depends, false, &depends_rel)) {
		return ir_err;
	}

	if (tgt->flags & custom_target_build_always_stale) {
		obj_array_push(wk, depends_rel, make_str(wk, "build_always_stale"));
	}

	obj depends = join_args_ninja(wk, depends_rel);

	obj_array_extend_nodup(wk, cmdline, tgt_args);

	outputs = join_args_ninja(wk, outputs);
	inputs = inputs ? join_args_ninja(wk, inputs) : make_str(wk, "");
	cmdline = join_args_shell_ninja(wk, cmdline);

	fprintf(ctx->out, "build %s: CUSTOM_COMMAND %s | %s\n"
		" COMMAND = %s\n"
		" DESCRIPTION = %s\n",
		get_cstr(wk, outputs),
		get_cstr(wk, inputs),
		get_cstr(wk, depends),
		get_cstr(wk, cmdline),
		get_cstr(wk, cmdline)
		);

	if (tgt->flags & custom_target_build_by_default) {
		ctx->wrote_default = true;
		fprintf(ctx->out, "default %s\n", get_cstr(wk, outputs));
	}

	fprintf(ctx->out, "\n");
	return ir_cont;
}

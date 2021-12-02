#include "posix.h"

#include "args.h"
#include "backend/ninja/custom_target.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/path.h"

static enum iteration_result
relativize_paths_iter(struct workspace *wk, void *_ctx, obj val)
{
	uint32_t *dest = _ctx;
	struct obj *file = get_obj(wk, val);

	if (file->type == obj_string) {
		obj_array_push(wk, *dest, val);
		return ir_cont;
	}

	assert(file->type == obj_file);

	char buf[PATH_MAX];

	if (!path_relative_to(buf, PATH_MAX, wk->build_root, get_cstr(wk, file->dat.file))) {
		return ir_err;
	}

	obj_array_push(wk, *dest, make_str(wk, buf));
	return ir_cont;
}

bool
ninja_write_custom_tgt(struct workspace *wk, const struct project *proj, obj tgt_id, FILE *out)
{
	struct obj *tgt = get_obj(wk, tgt_id);
	LOG_I("writing rules for custom target '%s'", get_cstr(wk, tgt->dat.custom_target.name));

	uint32_t outputs, inputs, cmdline;

	make_obj(wk, &inputs, obj_array);
	if (!obj_array_foreach(wk, tgt->dat.custom_target.input, &inputs, relativize_paths_iter)) {
		return ir_err;
	}

	make_obj(wk, &outputs, obj_array);
	if (!obj_array_foreach(wk, tgt->dat.custom_target.output, &outputs, relativize_paths_iter)) {
		return ir_err;
	}

	make_obj(wk, &cmdline, obj_array);
	obj_array_push(wk, cmdline, make_str(wk, wk->argv0));
	obj_array_push(wk, cmdline, make_str(wk, "internal"));
	obj_array_push(wk, cmdline, make_str(wk, "exe"));

	if (tgt->dat.custom_target.flags & custom_target_capture) {
		obj_array_push(wk, cmdline, make_str(wk, "-c"));

		uint32_t elem;
		if (!obj_array_index(wk, tgt->dat.custom_target.output, 0, &elem)) {
			assert(false && "custom target with no output");
			return ir_err;
		}

		if (relativize_paths_iter(wk, &cmdline, elem) == ir_err) {
			return ir_err;
		}
	}

	obj_array_push(wk, cmdline, make_str(wk, "--"));

	uint32_t tgt_args;
	if (!arr_to_args(wk, tgt->dat.custom_target.args, &tgt_args)) {
		return ir_err;
	}

	obj depends_rel;
	make_obj(wk, &depends_rel, obj_array);
	if (!obj_array_foreach(wk, tgt->dat.custom_target.depends, &depends_rel, relativize_paths_iter)) {
		return ir_err;
	}

	obj depends = join_args_ninja(wk, depends_rel);

	obj_array_extend(wk, cmdline, tgt_args);

	outputs = join_args_ninja(wk, outputs);
	inputs = join_args_ninja(wk, inputs);
	cmdline = join_args_shell_ninja(wk, cmdline);

	fprintf(out, "build %s: CUSTOM_COMMAND %s | %s\n"
		" COMMAND = %s\n"
		" DESCRIPTION = %s\n\n",
		get_cstr(wk, outputs),
		get_cstr(wk, inputs),
		get_cstr(wk, depends),
		get_cstr(wk, cmdline),
		get_cstr(wk, cmdline)
		);

	return ir_cont;
}

#include "posix.h"

#include "backend/ninja/build_target.h"
#include "backend/ninja/custom_target.h"
#include "backend/ninja/rules.h"
#include "backend/output.h"
#include "external/samu.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/path.h"

struct write_tgt_ctx {
	FILE *out;
	const struct project *proj;
};

static enum iteration_result
write_tgt_iter(struct workspace *wk, void *_ctx, uint32_t tgt_id)
{
	struct write_tgt_ctx *ctx = _ctx;

	switch (get_obj(wk, tgt_id)->type) {
	case obj_build_target:
		return ninja_write_build_tgt(wk, ctx->proj, tgt_id, ctx->out);
	case obj_custom_target:
		return ninja_write_custom_tgt(wk, ctx->proj, tgt_id, ctx->out);
	default:
		LOG_E("invalid tgt type '%s'", obj_type_to_s(get_obj(wk, tgt_id)->type));
		return ir_err;
	}
}

static bool
ninja_write_build(struct workspace *wk, void *_ctx, FILE *out)
{
	if (!ninja_write_rules(out, wk, darr_get(&wk->projects, 0))) {
		return false;
	}

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);

		struct write_tgt_ctx ctx = { .out = out, .proj = proj };

		if (!obj_array_foreach(wk, proj->targets, &ctx, write_tgt_iter)) {
			return false;
		}
	}

	return true;
}

static bool
ninja_write_tests(struct workspace *wk, void *_ctx, FILE *out)
{
	LOG_I("writing tests");

	obj tests;
	make_obj(wk, &tests, obj_dict);

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);

		if (proj->tests) {
			obj res, key;
			make_obj(wk, &key, obj_string)->dat.str = proj->cfg.name;

			if (obj_dict_index(wk, tests, key, &res)) {
				LOG_E("project defined multiple times");
				return false;
			}

			obj_dict_set(wk, tests, key, proj->tests);
		}
	}

	return serial_dump(wk, tests, out);
}

static bool
ninja_write_install(struct workspace *wk, void *_ctx, FILE *out)
{
	return serial_dump(wk, wk->install, out);
}

static bool
ninja_write_setup(struct workspace *wk, void *_ctx, FILE *f)
{
	struct project *proj;
	uint32_t i;
	obj opts;
	proj = darr_get(&wk->projects, 0);

	if (!obj_dict_dup(wk, proj->opts, &opts)) {
		return false;
	}

	for (i = 1; i < wk->projects.len; ++i) {
		proj = darr_get(&wk->projects, i);
		uint32_t str;
		make_obj(wk, &str, obj_string)->dat.str = proj->subproject_name;
		obj_dict_set(wk, opts, str, proj->opts);
	}

	fprintf(f, "error('build regeneration is broken :(')\n");
	return true;
}

bool
ninja_write_all(struct workspace *wk)
{
	if (!(with_open(wk->build_root, "build.ninja", wk, NULL, ninja_write_build)
	      && with_open(wk->muon_private, output_path.tests, wk, NULL, ninja_write_tests)
	      && with_open(wk->muon_private, output_path.install, wk, NULL, ninja_write_install)
	      && with_open(wk->muon_private, output_path.setup, wk, NULL, ninja_write_setup)
	      )) {
		return false;
	}

	/* compile_commands.json */
	if (have_samu) {
		char compile_commands[PATH_MAX];
		if (!path_join(compile_commands, PATH_MAX, wk->build_root, "compile_commands.json")) {
			return false;
		}

		if (!muon_samu_compdb(wk->build_root, compile_commands)) {
			return false;
		}
	}

	return true;
}

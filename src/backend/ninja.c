#include "posix.h"

#include "backend/ninja.h"
#include "backend/ninja/alias_target.h"
#include "backend/ninja/build_target.h"
#include "backend/ninja/custom_target.h"
#include "backend/ninja/rules.h"
#include "backend/output.h"
#include "external/samurai.h"
#include "functions/default/options.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/path.h"

struct write_tgt_ctx {
	FILE *out;
	const struct project *proj;
};

static enum iteration_result
write_tgt_iter(struct workspace *wk, void *_ctx, obj tgt_id)
{
	struct write_tgt_ctx *ctx = _ctx;

	enum obj_type t = get_obj_type(wk, tgt_id);
	switch (t) {
	case obj_alias_target:
		return ninja_write_alias_tgt(wk, tgt_id, ctx->out);
	case obj_build_target:
		return ninja_write_build_tgt(wk, ctx->proj, tgt_id, ctx->out);
	case obj_custom_target:
		return ninja_write_custom_tgt(wk, ctx->proj, tgt_id, ctx->out);
	default:
		LOG_E("invalid tgt type '%s'", obj_type_to_s(t));
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
	bool wrote_header = false;

	obj tests;
	make_obj(wk, &tests, obj_dict);

	uint32_t i;
	for (i = 0; i < wk->projects.len; ++i) {
		struct project *proj = darr_get(&wk->projects, i);

		if (proj->tests && get_obj_array(wk, proj->tests)->len) {
			if (!wrote_header) {
				LOG_I("writing tests");
				wrote_header = true;
			}

			obj res, key;
			key = proj->cfg.name;

			if (obj_dict_index(wk, tests, key, &res)) {
				assert("false" && "project defined multiple times");
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
	obj o;
	make_obj(wk, &o, obj_array);
	obj_array_push(wk, o, wk->install);
	obj_array_push(wk, o, wk->install_scripts);
	obj_array_push(wk, o, make_str(wk, wk->source_root));

	struct project *proj = darr_get(&wk->projects, 0);
	obj prefix;
	get_option(wk, proj, "prefix", &prefix);
	obj_array_push(wk, o, prefix);

	return serial_dump(wk, o, out);
}

static bool
ninja_write_opts(struct workspace *wk, void *_ctx, FILE *out)
{
	struct project *proj;
	uint32_t i;
	obj opts;
	make_obj(wk, &opts, obj_dict);

	for (i = 0; i < wk->projects.len; ++i) {
		proj = darr_get(&wk->projects, i);

		obj key;
		if (proj->subproject_name) {
			key = proj->subproject_name;
		} else {
			key = make_str(wk, "");
		}

		assert(!obj_dict_index(wk, opts, key, &(obj){ 0 }) && "project defined multiple times");

		obj_dict_set(wk, opts, key, proj->opts);
	}

	return serial_dump(wk, opts, out);
}

static bool
ninja_write_setup(struct workspace *wk, void *_ctx, FILE *out)
{
	fprintf(out,
		"setup(\n"
		"\t'%s',\n"
		"\tsource: '%s',\n"
		"\toptions: files('muon-private/opts.dat'),\n"
		")\n",
		wk->build_root,
		wk->source_root
		);
	return true;
}

bool
ninja_write_all(struct workspace *wk)
{
	if (!(with_open(wk->build_root, "build.ninja", wk, NULL, ninja_write_build)
	      && with_open(wk->muon_private, output_path.tests, wk, NULL, ninja_write_tests)
	      && with_open(wk->muon_private, output_path.install, wk, NULL, ninja_write_install)
	      && with_open(wk->muon_private, output_path.opts, wk, NULL, ninja_write_opts)
	      && with_open(wk->muon_private, output_path.setup, wk, NULL, ninja_write_setup)
	      )) {
		return false;
	}

	/* compile_commands.json */
	if (have_samurai) {
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

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "backend/output.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "tracy.h"

const struct output_path output_path = {
	.private_dir = ".muon",
	.introspect_dir = "meson-info",
	.meson_private_dir = "meson-private",
	.paths = {
		[output_path_summary] = { "summary.txt" },
		[output_path_tests] = { "tests.dat" },
		[output_path_install] = { "install.dat" },
		[output_path_compiler_check_cache] = { "compiler_check_cache.dat", .is_cache = true },
		[output_path_option_info] = { "option_info.dat" },
		[output_path_debug_log] = { "setup.log" },
		[output_path_vsenv_cache] = { "vsenv.txt", .is_cache = true },
	},
	.introspect_file = {
		.projectinfo = "intro-projectinfo.json",
		.targets = "intro-targets.json",
		.benchmarks = "intro-benchmarks.json",
		.buildoptions = "intro-buildoptions.json",
		.buildsystem_files = "intro-buildsystem_files.json",
		.compilers = "intro-compilers.json",
		.dependencies = "intro-dependencies.json",
		.scan_dependencies = "intro-scan_dependencies.json",
		.installed = "intro-installed.json",
		.install_plan = "intro-install_plan.json",
		.machines = "intro-machines.json",
		.tests = "intro-tests.json",
	},
};

FILE *
output_open(struct workspace *wk, const char *dir, const char *name)
{
	TSTR(path);
	path_join(wk, &path, dir, name);

	FILE *f = fs_fopen(path.buf, "wb");
	return f;
}

bool
with_open(const char *dir, const char *name, struct workspace *wk, void *ctx, with_open_callback cb)
{
	TracyCZone(tctx_func, true);
#ifdef TRACY_ENABLE
	char buf[4096] = { 0 };
	snprintf(buf, 4096, "with_open('%s')", name);
	TracyCZoneName(tctx_func, buf, strlen(buf));
#endif

	obj_array_push(wk, wk->backend_output_stack, make_strf(wk, "writing %s", name));

	bool ret = false;
	FILE *out;
	if (!(out = output_open(wk, dir, name))) {
		goto ret;
	} else if (!cb(wk, ctx, out)) {
		goto ret;
	} else if (!fs_fclose(out)) {
		goto ret;
	}

	ret = true;
ret:
	obj_array_pop(wk, wk->backend_output_stack);

	TracyCZoneEnd(tctx_func);
	return ret;
}

bool
output_clear_caches(struct workspace *wk)
{
	bool ok = true;
	uint32_t i;
	for (i = 0; i < output_path_name_count; ++i) {
		if (!output_path.paths[i].is_cache) {
			continue;
		}

		TSTR(path);
		path_join(wk, &path, wk->muon_private, output_path.paths[i].path);
		if (fs_file_exists(path.buf)) {
			if (!fs_remove(path.buf)) {
				ok = false;
			}
		}
	}

	return ok;
}

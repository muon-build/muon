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
	.summary = "summary.txt",
	.tests = "tests.dat",
	.install = "install.dat",
	.compiler_check_cache = "compiler_check_cache.dat",
	.option_info = "option_info.dat",
};

FILE *
output_open(const char *dir, const char *name)
{
	SBUF_manual(path);
	path_join(NULL, &path, dir, name);

	FILE *f = fs_fopen(path.buf, "wb");
	sbuf_destroy(&path);
	return f;
}

bool
with_open(const char *dir, const char *name, struct workspace *wk,
	void *ctx, with_open_callback cb)
{
	TracyCZone(tctx_func, true);
#ifdef TRACY_ENABLE
	char buf[4096] = { 0 };
	snprintf(buf, 4096, "with_open('%s')", name);
	TracyCZoneName(tctx_func, buf, strlen(buf));
#endif

	bool ret = false;
	FILE *out;
	if (!(out = output_open(dir, name))) {
		goto ret;
	} else if (!cb(wk, ctx, out)) {
		goto ret;
	} else if (!fs_fclose(out)) {
		goto ret;
	}

	ret = true;
ret:
	TracyCZoneEnd(tctx_func);
	return ret;
}

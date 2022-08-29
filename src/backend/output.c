#include "posix.h"

#include "backend/output.h"
#include "platform/filesystem.h"
#include "platform/path.h"

const struct output_path output_path = {
	.private_dir = "muon-private",
	.summary = "summary.txt",
	.tests = "tests.dat",
	.install = "install.dat",
	.compiler_check_cache = "compiler_check_cache.dat",
	.option_info = "option_info.dat",
};

FILE *
output_open(const char *dir, const char *name)
{
	SBUF_1k(path, sbuf_flag_overflow_alloc);
	path_join(NULL, &path, dir, name);

	FILE *f = fs_fopen(path.buf, "w");
	sbuf_destroy(&path);
	return f;
}

bool
with_open(const char *dir, const char *name, struct workspace *wk,
	void *ctx, with_open_callback cb)
{
	FILE *out;
	if (!(out = output_open(dir, name))) {
		return false;
	} else if (!cb(wk, ctx, out)) {
		return false;
	} else if (!fs_fclose(out)) {
		return false;
	}

	return true;
}

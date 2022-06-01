#include "posix.h"

#include "backend/output.h"
#include "platform/filesystem.h"
#include "platform/path.h"

const struct output_path output_path = {
	.private_dir = "muon-private",
	.summary = "summary.txt",
	.tests = "tests.dat",
	.install = "install.dat",
};

FILE *
output_open(const char *dir, const char *name)
{
	char path[PATH_MAX];
	if (!path_join(path, PATH_MAX, dir, name)) {
		return NULL;
	}

	return fs_fopen(path, "w");
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

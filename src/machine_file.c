#include "posix.h"

#include <string.h>

#include "eval.h"
#include "filesystem.h"
#include "inih.h"
#include "machine_file.h"
#include "mem.h"
#include "parser.h"
#include "workspace.h"

struct machine_info {
	uint32_t system, cpu_family, cpu, endian;
	struct {
		uint32_t c, ld, ar;
	} binaries;
};

enum machine_file_section {
	mfile_section_constants,
	mfile_section_binaries,
	mfile_section_properties,
	mfile_section_host_machine,
	machine_file_section_count,
};

static const char *machine_file_section_names[machine_file_section_count] = {
	[mfile_section_constants] = "constants",
	[mfile_section_binaries] = "binaries",
	[mfile_section_properties] = "properties",
	[mfile_section_host_machine] = "host_machine",
};

static bool
mfile_lookup(const char *val, const char *table[], uint32_t len, uint32_t *ret)
{
	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (strcmp(val, table[i]) == 0) {
			*ret = i;
			return true;
		}
	}

	return false;
}

struct machine_file_parse_ctx {
	struct workspace *wk;
};

static bool
machine_file_parse_cb(void *_ctx, struct source *src, const char *_sect,
	const char *k, const char *v, uint32_t line)
{
	struct machine_file_parse_ctx *ctx = _ctx;
	enum machine_file_section sect;

	if (!mfile_lookup(_sect, machine_file_section_names,
		machine_file_section_count, &sect)) {
		if (!k) {
			error_messagef(src, line, 1, "invalid section '%s'", _sect);
		}
		return false;
	} else if (!k) {
		hash_clear(&current_project(ctx->wk)->scope);
		return true;
	}

	if (!_sect) {
		error_messagef(src, line, 1, "key not under any section");
		return false;
	}

	struct source val_src = { .label = k, .src = v, .len = strlen(v), };

	uint32_t res;
	if (!eval(ctx->wk, &val_src, &res)) {
		error_messagef(src, line, 1, "failed to parse value");
		return false;
	}

	char buf[2048];
	if (!obj_to_s(ctx->wk, res, buf, 2048)) {
		return false;
	}

	if (sect == mfile_section_constants) {
		hash_set(&ctx->wk->scope, k, res);
	} else {
		hash_set(&current_project(ctx->wk)->scope, k, res);
	}

	printf("%s = %s\n", k, buf);

	return true;
}

bool
machine_file_parse(const char *path)
{
	bool ret = false;
	char *ini_buf;

	struct workspace wk;
	workspace_init(&wk);

	wk.lang_mode = language_internal;

	uint32_t id;
	make_project(&wk, &id, "dummy", wk.source_root, wk.build_root);

	struct machine_file_parse_ctx ctx = {
		.wk = &wk,
	};

	if (!ini_parse(path, &ini_buf, machine_file_parse_cb, &ctx)) {
		goto ret;
	}

	ret = true;
ret:
	if (ini_buf) {
		z_free(ini_buf);
	}
	workspace_destroy(&wk);
	return ret;

}

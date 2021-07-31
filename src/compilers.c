#include "posix.h"

#include <stdlib.h>
#include <string.h>

#include "compilers.h"
#include "log.h"
#include "run_cmd.h"
#include "workspace.h"

const char *
compiler_type_to_s(enum compiler_type t)
{
	switch (t) {
	case compiler_gcc: return "gcc";
	case compiler_clang: return "clang";
	default: assert(false); return "";
	}
}

bool
compiler_detect_c(struct workspace *wk, uint32_t *comp_id)
{
	const char *cc;
	if (!(cc = getenv("CC"))) {
		cc = "cc";
	}

	// helpful: mesonbuild/compilers/detect.py:350
	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd(&cmd_ctx, cc, (char *[]){
		(char *)cc, "--version", NULL,
	})) {
		return false;
	}

	enum compiler_type type;
	if (strstr(cmd_ctx.out, "clang") || strstr(cmd_ctx.out, "Clang")) {
		type = compiler_clang;
	} else if (strstr(cmd_ctx.out, "Free Software Foundation")) {
		type = compiler_gcc;
	} else {
		LOG_E("unknown compiler: '%s'", cmd_ctx.out);
		return false;
	}

	char *p;
	if ((p = strchr(cmd_ctx.out, '\n'))) {
		*p = 0;
	}

	char *ver, *ver_end;
	for (ver = cmd_ctx.out; *ver; ++ver) {
		ver_end = ver;
		while (('0' <= *ver_end && *ver_end <= '9') || *ver_end == '.') {
			++ver_end;
		}

		if (ver != ver_end) {
			*ver_end = 0;
			break;
		}
	}

	LOG_I("detected C compiler %s %s", compiler_type_to_s(type), ver);

	struct obj *comp = make_obj(wk, comp_id, obj_compiler);
	comp->dat.compiler.name = wk_str_push(wk, cc);
	comp->dat.compiler.type = type;
	comp->dat.compiler.version = wk_str_push(wk, ver);
	return true;
}

const struct compiler compilers[] = {
	[compiler_gcc] = {
		.command = "$ARGS -MD -MQ $out -MF $DEPFILE -o $out -c $in",
		.deps = "gcc",
		.depfile = "$DEPFILE_UNQUOTED",
		.description = "Compiling C object $out",
	},
	[compiler_clang] = {
		.command = "$ARGS -MD -MQ $out -MF $DEPFILE -o $out -c $in",
		.deps = "gcc",
		.depfile = "$DEPFILE_UNQUOTED",
		.description = "Compiling C object $out",
	},
};

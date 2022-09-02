#include "posix.h"

#include "args.h"
#include "backend/ninja.h"
#include "backend/ninja/alias_target.h"
#include "lang/object.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"

bool
ninja_write_alias_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *ctx)
{
	struct obj_alias_target *tgt = get_obj_alias_target(wk, tgt_id);

	L("writing rules for alias target '%s'", get_cstr(wk, tgt->name));

	SBUF(name_esc);
	ninja_escape(wk, &name_esc, get_cstr(wk, tgt->name));

	obj depstrs;
	if (!arr_to_args(wk, arr_to_args_alias_target | arr_to_args_build_target
		| arr_to_args_custom_target | arr_to_args_relativize_paths,
		tgt->depends, &depstrs)) {
		return false;
	}
	obj depstr = join_args_ninja(wk, depstrs);

	if (fprintf(ctx->out, "build %s: phony | %s\n\n", name_esc.buf, get_cstr(wk, depstr)) < 0) {
		return false;
	}

	return true;
}

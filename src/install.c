#include "posix.h"

#include "backend/output.h"
#include "install.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static enum iteration_result
install_iter(struct workspace *wk, void *_ctx, uint32_t v_id)
{
	struct obj *in = get_obj(wk, v_id);
	assert(in->type == obj_install_target);

	LOG_I("TODO: install '%s'", wk_str(wk, in->dat.install_target.filename));
	return ir_cont;
}

bool
install_run(const char *build_root)
{
	bool ret = true;
	char install_src[PATH_MAX], private[PATH_MAX];
	if (!path_join(private, PATH_MAX, build_root, output_path.private_dir)) {
		return false;
	} else if (!path_join(install_src, PATH_MAX, private, output_path.install)) {
		return false;
	}

	FILE *f;
	if (!(f = fs_fopen(install_src, "r"))) {
		return false;
	}

	struct workspace wk;
	workspace_init_bare(&wk);

	uint32_t install_arr;
	if (!serial_load(&wk, &install_arr, f)) {
		LOG_E("failed to load %s", output_path.install);
		goto ret;
	} else if (!fs_fclose(f)) {
		goto ret;
	}

	obj_array_foreach(&wk, install_arr, NULL, install_iter);

	ret = true;
ret:
	workspace_destroy_bare(&wk);
	return ret;
}

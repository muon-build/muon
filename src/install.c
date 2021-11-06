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
	struct install_options *opts = _ctx;
	struct obj *in = get_obj(wk, v_id);
	assert(in->type == obj_install_target);

	char src_buf[PATH_MAX], filename_buf[PATH_MAX], dest[PATH_MAX];
	const char *dest_dir = get_cstr(wk, in->dat.install_target.install_dir),
		   *src;

	if (in->dat.install_target.base_path) {
		if (!path_join(src_buf, PATH_MAX, get_cstr(wk, in->dat.install_target.base_path),
			get_cstr(wk, in->dat.install_target.filename))) {
			return ir_err;
		}

		src = src_buf;
	} else {
		src = get_cstr(wk, in->dat.install_target.filename);

	}

	assert(path_is_absolute(src));
	if (!path_basename(filename_buf, PATH_MAX, src)) {
		return ir_err;
	}

	if (!path_join(dest, PATH_MAX, dest_dir, filename_buf)) {
		return ir_err;
	}

	LOG_I("install '%s' -> '%s'", src, dest);

	if (opts->dry_run) {
		return ir_cont;
	}

	if (fs_dir_exists(src)) {
		char basedir[PATH_MAX];
		if (!path_dirname(basedir, PATH_MAX, dest)) {
			return ir_err;
		}

		if (!fs_mkdir_p(basedir)) {
			return ir_err;
		}

		if (!fs_copy_dir(src, dest)) {
			return ir_err;
		}
	} else {
		if (fs_exists(dest_dir) && !fs_dir_exists(dest_dir)) {
			LOG_E("dest '%s' exists and is not a directory", dest_dir);
			return ir_err;
		}

		if (!fs_mkdir_p(dest_dir)) {
			return ir_err;
		}

		if (!fs_copy_file(src, dest)) {
			return ir_err;
		}
	}

	return ir_cont;
}

bool
install_run(const char *build_root, struct install_options *opts)
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

	obj install;
	if (!serial_load(&wk, &install, f)) {
		LOG_E("failed to load %s", output_path.install);
		goto ret;
	} else if (!fs_fclose(f)) {
		goto ret;
	}

	obj install_targets, install_scripts;
	obj_array_index(&wk, install, 0, &install_targets);
	obj_array_index(&wk, install, 1, &install_scripts);

	obj_array_foreach(&wk, install_targets, opts, install_iter);

	ret = true;
ret:
	workspace_destroy_bare(&wk);
	return ret;
}

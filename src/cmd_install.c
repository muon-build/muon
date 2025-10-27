/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: illiliti <illiliti@dimension.sh>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "args.h"
#include "backend/output.h"
#include "cmd_install.h"
#include "error.h"
#include "functions/environment.h"
#include "lang/object_iterators.h"
#include "lang/serial.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "platform/rpath_fixer.h"
#include "platform/run_cmd.h"

struct install_ctx {
	struct install_options *opts;
	obj prefix;
	obj full_prefix;
	obj destdir;
	obj env;
	obj installed;
};

enum install_action {
	install_action_mkdir,
	install_action_mkdir_p,
	install_action_copy_file,
	install_action_make_symlink,
	install_action_chmod,
	install_action_fix_rpaths,
};

static const char *
install_action_to_s(enum install_action action)
{
	switch (action) {
	case install_action_mkdir: return "mkdir";
	case install_action_mkdir_p: return "mkdir_p";
	case install_action_copy_file: return "copy_file";
	case install_action_make_symlink: return "make_symlink";
	case install_action_chmod: return "chmod";
	case install_action_fix_rpaths: return "fix_rpaths";
	}

	UNREACHABLE_RETURN;
}

static void
print_install_action(struct workspace *wk, enum install_action action, obj src, obj dest)
{
	switch (action) {
	case install_action_mkdir:
	case install_action_mkdir_p: obj_lprintf(wk, log_info, "%s %o\n", install_action_to_s(action), dest); break;
	case install_action_copy_file:
		obj_lprintf(wk, log_info, "%s %o -> %o\n", install_action_to_s(action), src, dest);
		break;
	case install_action_make_symlink:
		obj_lprintf(wk, log_info, "%s %o\n", install_action_to_s(action), dest);
		break;
	case install_action_chmod:
		LLOG_I("%s %03o", install_action_to_s(action), (int)get_obj_number(wk, src));
		obj_lprintf(wk, log_info, " %o\n", dest);
		break;
	case install_action_fix_rpaths: obj_lprintf(wk, log_info, "%s %o\n", install_action_to_s(action), dest); break;
	}
}

static void
record_install_action(struct workspace *wk, struct install_ctx *ctx, enum install_action action, obj src, obj dest)
{
	obj a = make_obj(wk, obj_array);
	obj_array_push(wk, a, make_number(wk, action));
	obj_array_push(wk, a, src);
	obj_array_push(wk, a, dest);
	obj_array_push(wk, ctx->installed, a);

	print_install_action(wk, action, src, dest);
}

static bool
do_install_action(struct workspace *wk,
	struct install_ctx *ctx,
	enum install_action action,
	const char *src,
	const char *dest,
	uint32_t flags)
{
	if (ctx->opts->dry_run) {
		LLOG_I("%s", install_action_to_s(action));
		if (src) {
			log_print(false, log_info, " %s", src);
		}
		if (dest) {
			log_print(false, log_info, " ->%s", dest);
		}
		if (flags) {
			log_print(false, log_info, " %03o", flags);
		}
		log_print(false, log_info, "\n");
		return true;
	}

	switch (action) {
	case install_action_mkdir:
	case install_action_mkdir_p: {
		obj record = make_obj(wk, obj_array);
		if (!fs_mkdir_p_recorded(wk, dest, record)) {
			return false;
		}

		obj v;
		obj_array_for(wk, record, v) {
			record_install_action(wk, ctx, install_action_mkdir, 0, v);
		}
		break;
	}
	case install_action_copy_file:
		if (!fs_copy_file(wk, src, dest, true)) {
			return false;
		}

		record_install_action(wk, ctx, action, make_str(wk, src), make_str(wk, dest));
		break;
	case install_action_make_symlink:
		if (!fs_make_symlink(src, dest, true)) {
			return false;
		}

		record_install_action(wk, ctx, action, make_str(wk, src), make_str(wk, dest));
		break;
	case install_action_chmod:
		if (!fs_chmod(dest, flags)) {
			return false;
		}

		record_install_action(wk, ctx, action, make_number(wk, flags), make_str(wk, dest));
		break;
	case install_action_fix_rpaths:
		if (ctx->opts->dry_run) {
			break;
		} else {
			if (!fix_rpaths(wk, dest, src)) {
				return ir_err;
			}

			record_install_action(wk, ctx, action, make_str(wk, src), make_str(wk, dest));
		}
		break;
	}

	return true;
}

struct install_dir_ctx {
	obj exclude_directories;
	obj exclude_files;
	bool has_perm;
	uint32_t perm;
	const char *src_base, *dest_base;
	const char *src_root;
	struct install_ctx *ctx;
	struct workspace *wk;
};

static enum iteration_result
install_dir_iter(void *_ctx, const char *path)
{
	struct install_dir_ctx *ctx = _ctx;
	TSTR(src);
	TSTR(dest);

	path_join(ctx->wk, &src, ctx->src_base, path);
	path_join(ctx->wk, &dest, ctx->dest_base, path);

	TSTR(rel);
	path_relative_to(ctx->wk, &rel, ctx->src_root, src.buf);
	obj rel_str = tstr_into_str(ctx->wk, &rel);

	if (fs_dir_exists(src.buf)) {
		if (ctx->exclude_directories && obj_array_in(ctx->wk, ctx->exclude_directories, rel_str)) {
			LOG_I("skipping dir '%s'", src.buf);
			return ir_cont;
		}

		if (!do_install_action(ctx->wk, ctx->ctx, install_action_mkdir, 0, dest.buf, 0)) {
			return ir_err;
		}

		struct install_dir_ctx new_ctx = {
			.exclude_directories = ctx->exclude_directories,
			.exclude_files = ctx->exclude_files,
			.has_perm = ctx->has_perm,
			.perm = ctx->perm,
			.src_root = ctx->src_root,
			.src_base = src.buf,
			.dest_base = dest.buf,
			.ctx = ctx->ctx,
			.wk = ctx->wk,
		};

		if (!fs_dir_foreach(ctx->wk, src.buf, &new_ctx, install_dir_iter)) {
			return ir_err;
		}
	} else if (fs_symlink_exists(src.buf) || fs_file_exists(src.buf)) {
		if (ctx->exclude_files && obj_array_in(ctx->wk, ctx->exclude_files, rel_str)) {
			LOG_I("skipping file '%s'", src.buf);
			return ir_cont;
		}

		if (!do_install_action(ctx->wk, ctx->ctx, install_action_copy_file, src.buf, dest.buf, 0)) {
			return ir_err;
		}
	} else {
		LOG_E("unhandled file type '%s'", path);
		return ir_err;
	}

	if (ctx->has_perm && !do_install_action(ctx->wk, ctx->ctx, install_action_chmod, 0, dest.buf, ctx->perm)) {
		return ir_err;
	}

	return ir_cont;
}

static enum iteration_result
install_iter(struct workspace *wk, void *_ctx, obj v_id)
{
	struct install_ctx *ctx = _ctx;
	struct obj_install_target *in = get_obj_install_target(wk, v_id);

	TSTR(dest_dirname);
	const char *dest = get_cstr(wk, in->dest), *src = get_cstr(wk, in->src);

	assert(in->type == install_target_symlink || in->type == install_target_emptydir || path_is_absolute(src));

	TSTR(full_dest_dir);
	if (ctx->destdir) {
		path_join_absolute(wk, &full_dest_dir, get_cstr(wk, ctx->destdir), dest);
		dest = full_dest_dir.buf;
	}

	// meson creates empty directories for dirs that don't exist in the source
	// tree
	if (in->type == install_target_subdir && !fs_dir_exists(src)) {
		in->type = install_target_emptydir;
	}

	if ((in->type == install_target_default) && !fs_exists(src)) {
		LOG_W("src '%s' does not exist, skipping", src);
		return ir_cont;
	}

	switch (in->type) {
	case install_target_default:
	case install_target_symlink: {
		path_dirname(wk, &dest_dirname, dest);

		if (fs_exists(dest_dirname.buf) && !fs_dir_exists(dest_dirname.buf)) {
			LOG_E("dest '%s' exists and is not a directory", dest_dirname.buf);
			return ir_err;
		}

		if (!do_install_action(wk, ctx, install_action_mkdir_p, 0, dest_dirname.buf, 0)) {
			return ir_err;
		}

		if (in->type == install_target_default) {
			if (fs_dir_exists(src)) {
				if (!do_install_action(wk, ctx, install_action_mkdir_p, 0, dest, 0)) {
					return ir_err;
				}

				struct install_dir_ctx dir_ctx = {
					.exclude_directories = in->exclude_directories,
					.exclude_files = in->exclude_files,
					.has_perm = in->has_perm,
					.perm = in->perm,
					.src_root = src,
					.src_base = src,
					.dest_base = dest,
					.ctx = ctx,
					.wk = wk,
				};

				if (!fs_dir_foreach(wk, src, &dir_ctx, install_dir_iter)) {
					return ir_err;
				}
			} else {
				if (!do_install_action(wk, ctx, install_action_copy_file, src, dest, 0)) {
					return ir_err;
				}
			}

			if (in->build_target) {
				if (!do_install_action(wk, ctx, install_action_fix_rpaths, wk->build_root, dest, 0)) {
					return ir_err;
				}
			}
		} else {
			if (!do_install_action(wk, ctx, install_action_make_symlink, src, dest, 0)) {
				return ir_err;
			}
		}
		break;
	}
	case install_target_subdir: {
		if (!do_install_action(wk, ctx, install_action_mkdir_p, 0, dest, 0)) {
			return ir_err;
		}

		struct install_dir_ctx dir_ctx = {
			.exclude_directories = in->exclude_directories,
			.exclude_files = in->exclude_files,
			.has_perm = in->has_perm,
			.perm = in->perm,
			.src_root = src,
			.src_base = src,
			.dest_base = dest,
			.ctx = ctx,
			.wk = wk,
		};

		if (!fs_dir_foreach(wk, src, &dir_ctx, install_dir_iter)) {
			return ir_err;
		}
		break;
	}
	case install_target_emptydir: {
		if (!do_install_action(wk, ctx, install_action_mkdir_p, 0, dest, 0)) {
			return ir_err;
		}
		break;
	}
	default: UNREACHABLE_RETURN;
	}

	if (in->has_perm && !do_install_action(wk, ctx, install_action_chmod, 0, dest, in->perm)) {
		return ir_err;
	}

	return ir_cont;
}

static void
install_script_env_set(struct workspace *wk, obj env, const char *k, obj v)
{
	if (!environment_set(wk, env, environment_set_mode_set, make_str(wk, k), v, 0)) {
		UNREACHABLE;
	}
}

static enum iteration_result
install_scripts_iter(struct workspace *wk, void *_ctx, obj install_script)
{
	struct install_ctx *ctx = _ctx;

	obj install_script_skip_if_destdir, install_script_dry_run, install_script_cmdline;
	install_script_skip_if_destdir = obj_array_index(wk, install_script, 0);
	install_script_dry_run = obj_array_index(wk, install_script, 1);
	install_script_cmdline = obj_array_index(wk, install_script, 2);

	bool script_skip_if_destdir = get_obj_bool(wk, install_script_skip_if_destdir);
	bool script_can_dry_run = get_obj_bool(wk, install_script_dry_run);

	obj env;
	{
		env = make_obj_environment(wk, 0);

		environment_extend(wk, env, ctx->env);

		if (ctx->destdir) {
			install_script_env_set(wk, env, "DESTDIR", ctx->destdir);
		}
		install_script_env_set(wk, env, "MESON_INSTALL_PREFIX", ctx->prefix);
		install_script_env_set(wk, env, "MESON_INSTALL_DESTDIR_PREFIX", ctx->full_prefix);
		if (ctx->opts->dry_run && script_can_dry_run) {
			install_script_env_set(wk, env, "MESON_INSTALL_DRY_RUN", make_str(wk, "1"));
		}
	}

	const char *argstr, *envstr;
	uint32_t argc, envc;
	env_to_envstr(wk, &envstr, &envc, env);
	join_args_argstr(wk, &argstr, &argc, install_script_cmdline);

	if (ctx->destdir && script_skip_if_destdir) {
		LOG_I("skipping install script because DESTDIR is set '%s'", argstr);
		return ir_cont;
	}

	LOG_I("running install script '%s'", argstr);

	if (ctx->opts->dry_run && !script_can_dry_run) {
		return ir_cont;
	}

	struct run_cmd_ctx cmd_ctx = { 0 };
	if (!run_cmd(wk, &cmd_ctx, argstr, argc, envstr, envc)) {
		LOG_E("failed to run install script: %s", cmd_ctx.err_msg);
		goto err;
	}

	if (cmd_ctx.status != 0) {
		LOG_E("install script failed");
		LOG_E("stdout: %s", cmd_ctx.out.buf);
		LOG_E("stderr: %s", cmd_ctx.err.buf);
		goto err;
	}

	run_cmd_ctx_destroy(&cmd_ctx);
	return ir_cont;
err:
	run_cmd_ctx_destroy(&cmd_ctx);
	return ir_err;
}

static bool
do_uninstall(struct workspace *wk, struct install_ctx *ctx, struct install_options *opts)
{
	TSTR(installed_src);
	path_join(wk, &installed_src, output_path.private_dir, output_path.paths[output_path_installed].path);

	if (!fs_file_exists(installed_src.buf)) {
		LOG_E("unable to find record of previous install at %s", installed_src.buf);
		return false;
	}

	obj installed;
	if (!serial_load_from_private_dir(wk, &installed, output_path.paths[output_path_installed].path)) {
		return false;
	}

	uint32_t installed_len = get_obj_array(wk, installed)->len;

	if (!installed_len) {
		LOG_I("nothing to uninstall");
		return true;
	}

	struct uninstall_elem {
		obj record;
		bool ok;
	};

	struct arr installed_arr = { 0 };
	arr_init(wk->a_scratch, &installed_arr, installed_len, struct uninstall_elem);

	obj v;
	obj_array_for(wk, installed, v) {
		arr_push(wk->a_scratch, &installed_arr, &(struct uninstall_elem) { .record = v, .ok = true });
	}

	bool all_ok = true;
	obj action_dedup = make_obj(wk, obj_dict);

	for (int32_t i = installed_arr.len - 1; i >= 0; --i) {
		struct uninstall_elem *e = arr_get(&installed_arr, i);
		enum install_action action = get_obj_number(wk, obj_array_index(wk, e->record, 0));
		obj d = obj_array_index(wk, e->record, 2);
		const char *dest = get_cstr(wk, d);

		// print_install_action(wk, action, obj_array_index(wk, e->record, 1), d);
		// LOG_I("");

		bool ok = true;

		switch (action) {
		case install_action_mkdir_p:
		case install_action_mkdir:
			LOG_I("rmdir %s", dest);
			if (!opts->dry_run && !fs_rmdir(dest, true)) {
				ok = false;
			}
			break;
		case install_action_make_symlink:
		case install_action_copy_file:
			if (obj_dict_in(wk, action_dedup, d)) {
				continue;
			}
			obj_dict_set(wk, action_dedup, d, obj_bool_true);

			LOG_I("rm %s", dest);
			if (!opts->dry_run && !fs_remove(dest)) {
				ok = false;
			}
			break;
		case install_action_chmod:
		case install_action_fix_rpaths:
			/* no-op */
			break;
		}

		if (!ok) {
			e->ok = false;
			all_ok = false;
		}
	}

	for (uint32_t i = 0; i < installed_arr.len; ++i) {
		struct uninstall_elem *e = arr_get(&installed_arr, i);
		if (!e->ok) {
			obj_array_push(wk, ctx->installed, e->record);
		}
	}

	return all_ok;
}

static bool
write_installed(struct workspace *wk, void *_ctx, FILE *out)
{
	struct install_ctx *ctx = _ctx;
	return serial_dump(wk, ctx->installed, out);
}

bool
install_run(struct workspace *wk, struct install_options *opts)
{
	bool ret = false;
	TSTR(install_src);
	path_join(wk, &install_src, output_path.private_dir, output_path.paths[output_path_install].path);

	struct install_ctx ctx = {
		.opts = opts,
		.installed = make_obj(wk, obj_array),
	};

	if (opts->uninstall) {
		if (!(ret = do_uninstall(wk, &ctx, opts))) {
			LOG_E("uninstall finished with errors");
		}
		goto ret;
	}

	FILE *f;
	f = fs_fopen(install_src.buf, "rb");

	if (!f) {
		goto ret;
	}

	obj install;
	if (!serial_load(wk, &install, f)) {
		LOG_E("failed to load %s", output_path.paths[output_path_install].path);
		goto ret;
	} else if (!fs_fclose(f)) {
		goto ret;
	}

	obj install_targets, install_scripts, source_root;
	install_targets = obj_array_index(wk, install, 0);
	install_scripts = obj_array_index(wk, install, 1);
	source_root = obj_array_index(wk, install, 2);
	ctx.prefix = obj_array_index(wk, install, 3);
	ctx.env = obj_array_index(wk, install, 4);

	TSTR(build_root);
	path_copy_cwd(wk, &build_root);
	wk->build_root = get_cstr(wk, tstr_into_str(wk, &build_root));
	wk->source_root = get_cstr(wk, source_root);

	if ((opts->destdir)) {
		TSTR(full_prefix);
		TSTR(abs_destdir);
		path_make_absolute(wk, &abs_destdir, opts->destdir);
		path_join_absolute(wk, &full_prefix, abs_destdir.buf, get_cstr(wk, ctx.prefix));

		ctx.full_prefix = tstr_into_str(wk, &full_prefix);
		ctx.destdir = tstr_into_str(wk, &abs_destdir);
	} else {
		ctx.full_prefix = ctx.prefix;
	}

	if (!obj_array_foreach(wk, install_targets, &ctx, install_iter)) {
		goto ret;
	}

	if (!obj_array_foreach(wk, install_scripts, &ctx, install_scripts_iter)) {
		goto ret;
	}

	ret = true;
ret:
	if (!with_open(
		    output_path.private_dir, output_path.paths[output_path_installed].path, wk, &ctx, write_installed)) {
		ret = false;
	}

	return ret;
}

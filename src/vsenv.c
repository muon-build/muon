/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "backend/output.h"
#include "buf_size.h"
#include "lang/string.h"
#include "log.h"
#include "machines.h"
#include "platform/filesystem.h"
#include "platform/os.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "vsenv.h"

static const char *vsenv_list_sep = "---SPLIT---";

static void
vsenv_set_vars(struct workspace *wk, const char *vars, uint32_t len)
{
	bool sep_seen = false;
	uint32_t i;
	for (i = 0; i < len;) {
		const char *line = vars + i, *p = strstr(line, "\r\n");
		if (!p) {
			break;
		}
		uint32_t line_len = p - line;
		struct str l = { line, line_len }, k, v;

		if (!sep_seen) {
			if (str_eql(&l, &STRL(vsenv_list_sep))) {
				sep_seen = true;
			}
		} else if (str_split_in_two(&l, &k, &v, '=')) {
			os_set_env(wk, &k, &v);
		}

		i += line_len + 2;
	}
}

static bool
vsenv_setup(struct workspace *wk, const char *cache_path, enum setup_platform_env_requirement req)
{
	TSTR(_buf);

	if (cache_path && fs_file_exists(cache_path)) {
		struct source src;
		if (fs_read_entire_file(wk->a_scratch, cache_path, &src)) {
			vsenv_set_vars(wk, src.src, src.len);
			return true;
		}
	}

	if (req == setup_platform_env_requirement_from_cache) {
		return true;
	}

	if (os_get_env("VSINSTALLDIR")) {
		return true;
	} else if (fs_find_cmd(wk, &_buf, "cl.exe")) {
		return true;
	}

	if (req != setup_platform_env_requirement_required) {
		const char *comps[] = {
			"cc",
			"gcc",
			"clang",
			"clang-cl",
		};
		uint32_t i;
		for (i = 0; i < ARRAY_LEN(comps); ++i) {
			if (fs_find_cmd(wk, &_buf, comps[i])) {
				return true;
			}
		}
	}

	bool res = false;
	char tmp_path[512] = { 0 };
	TSTR(path);
	TSTR(ver);
	const char *program_files;
	struct run_cmd_ctx vswhere_cmd_ctx = { 0 }, bat_cmd_ctx = { 0 };
	uint32_t i;

	if (!(program_files = os_get_env("ProgramFiles(x86)"))) {
		LOG_E("vsenv: unable to get value of 'ProgramFiles(x86)' env var");
		goto ret;
	}

	path_join(wk, &path, program_files, "Microsoft Visual Studio/Installer/vswhere.exe");

	if (!fs_file_exists(path.buf)) {
		LOG_E("vsenv: vswhere.exe not found @ %s", path.buf);
		goto ret;
	}

	if (!run_cmd_argv(wk, &vswhere_cmd_ctx,
		    (char *const[]){
			    path.buf,
			    "-latest",
			    "-prerelease",
			    "-requiresAny",
			    "-requires",
			    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
			    "-requires",
			    "Microsoft.VisualStudio.Workload.WDExpress",
			    "-products",
			    "*",
			    "-utf8",
			    0,
		    },
		    0,
		    0)) {
		LOG_E("vsenv: failed to execute vswhere");
		goto ret;
	}

	if (vswhere_cmd_ctx.status != 0) {
		LOG_E("vsenv: exited with error status %d", vswhere_cmd_ctx.status);
		goto ret;
	}

	tstr_clear(&path);

	struct str installation_path = { 0 }, installation_name = { 0 };
	struct {
		struct str label, *dest;
	} keys[] = {
		{ STR("installationPath: "), &installation_path },
		{ STR("installationName: "), &installation_name },
	};
	for (i = 0; i < vswhere_cmd_ctx.out.len;) {
		const char *line = vswhere_cmd_ctx.out.buf + i, *p = strstr(line, "\r\n");
		if (!p) {
			break;
		}
		uint32_t line_len = p - line;
		struct str l = { line, line_len };

		for (uint32_t i = 0; i < ARRAY_LEN(keys); ++i) {
			if (str_startswith(&l, &keys[i].label)) {
				l.s += keys[i].label.len;
				l.len -= keys[i].label.len;
				*keys[i].dest = l;
				break;
			}
		}

		if (installation_name.len && installation_path.len) {
			break;
		}

		i += line_len + 2;
	}

	if (!(installation_name.len && installation_path.len)) {
		LOG_E("vsenv: failed to parse vswhere output");
		goto ret;
	}

	tstr_clear(&path);
	tstr_pushn(wk, &path, installation_path.s, installation_path.len);

	if (strcmp(build_machine.cpu, "arm64") == 0) {
		path_push(wk, &path, "VC/Auxiliary/Build/vcvarsarm64.bat");
		if (!fs_file_exists(path.buf)) {
			tstr_clear(&path);
			tstr_pushn(wk, &path, installation_path.s, installation_path.len);
			path_push(wk, &path, "VC/Auxiliary/Build/vcvarsx86_arm64.bat");
		}
	} else {
		path_push(wk, &path, "VC/Auxiliary/Build/vcvars64.bat");
		// if VS is not found try VS Express
		if (!fs_file_exists(path.buf)) {
			tstr_clear(&path);
			tstr_pushn(wk, &path, installation_path.s, installation_path.len);
			path_push(wk, &path, "VC/Auxiliary/Build/vcvarsx86_amd64.bat");
		}
	}

	if (!fs_file_exists(path.buf)) {
		LOG_E("vsenv: failed to locate vcvars @ %s", path.buf);
		goto ret;
	}

	L("vsenv: loading %.*s @ %s", installation_name.len, installation_name.s, path.buf);

	FILE *tmp = fs_make_tmp_file("vsenv_activate", ".bat", tmp_path, sizeof(tmp_path));
	if (!tmp) {
		LOG_E("vsenv: failed to create temporary file");
		goto ret;
	}

	fprintf(tmp,
		"@ECHO OFF\r\n"
		"call \"%s\"\r\n"
		"ECHO %s\r\n"
		"SET\r\n",
		path.buf,
		vsenv_list_sep);
	fs_fclose(tmp);

	if (!run_cmd_argv(wk, &bat_cmd_ctx, (char *const[]){ tmp_path, 0 }, 0, 0)) {
		LOG_E("vsenv: failed to execute %s", tmp_path);
		goto ret;
	}

	vsenv_set_vars(wk, bat_cmd_ctx.out.buf, bat_cmd_ctx.out.len);

	if (cache_path) {
		L("writing %s", cache_path);
		if (!fs_write(cache_path, (const uint8_t *)bat_cmd_ctx.out.buf, bat_cmd_ctx.out.len)) {
			goto ret;
		}
	}

	res = true;
ret:
	run_cmd_ctx_destroy(&vswhere_cmd_ctx);
	run_cmd_ctx_destroy(&bat_cmd_ctx);
	if (*tmp_path) {
		fs_remove(tmp_path);
	}
	return res;
}

void
setup_platform_env(struct workspace *wk, const char *build_dir, enum setup_platform_env_requirement req)
{
	if (req == setup_platform_env_requirement_skip) {
		return;
	}

	if (host_machine.sys == machine_system_windows) {
		TSTR(cache);
		const char *cache_path = 0;
		if (build_dir) {
			path_copy(wk, &cache, build_dir);
			path_push(wk, &cache, output_path.private_dir);
			fs_mkdir_p(wk, cache.buf);
			if (fs_dir_exists(cache.buf)) {
				path_push(wk, &cache, output_path.paths[output_path_vsenv_cache].path);
				cache_path = cache.buf;
			}
		}

		vsenv_setup(wk, cache_path, req);
	}
}

#include <string.h>
#include <assert.h>

#include <windows.h>
#define STRSAFE_NO_CB_FUNCTIONS
#include <strsafe.h>

#include "args.h"
#include "buf_size.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "platform/windows/win32_error.h"

#define KEY_PATH "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"

#define CLOSE_PIPE(p_) do { \
	if ((p_) != INVALID_HANDLE_VALUE && !CloseHandle(p_)) { \
		LOG_E("failed to close pipe: %s", win32_error()); \
	} \
	p_ = INVALID_HANDLE_VALUE; \
  } while (0)

#define COPY_PIPE_BLOCK_SIZE BUF_SIZE_1k

enum copy_pipe_result {
	copy_pipe_result_finished,
	copy_pipe_result_waiting,
	copy_pipe_result_failed,
};

/*
 * [X] copy_pipe()
 * [X] copy_pipes()
 * [X] run_cmd_ctx_close_pipes()
 * [X] run_cmd_collect()
 * [X] open_run_cmd_pipe()
 * [ ] run_cmd_internal()
 * [X] run_cmd_argv()
 * [X] run_cmd()
 * [X] run_cmd_ctx_destroy()
 * [ ] run_cmd_kill()
 */

static enum copy_pipe_result
copy_pipe(HANDLE pipe, struct run_cmd_pipe_ctx *ctx)
{
	if (!ctx->size) {
		ctx->size = COPY_PIPE_BLOCK_SIZE;
		ctx->len = 0;
		ctx->buf = z_calloc(1, ctx->size + 1);
	}

	while (true) {
		DWORD bytes_read;
		BOOL res;

		res = ReadFile(pipe, &ctx->buf[ctx->len], ctx->size - ctx->len, &bytes_read, NULL);

		if (!res || bytes_read == 0UL) {
			char *buf;
			char *it1;
			char *it2;
			size_t len;

			buf = z_calloc(1, ctx->len + 1);
			it1 = ctx->buf;
			it2 = buf;
			len = 0;
			for (it1 = ctx->buf; *it1; it1++) {
				if (*it1 == '\r' && *(it1 + 1) == '\n') {
					continue;
				}

				*it2 = *it1;
				it2++;
				len++;
			}
			z_free(ctx->buf);
			ctx->buf = buf;
			ctx->len = len;
			LOG_E(" ** resultat : '%s'", ctx->buf);
			return copy_pipe_result_finished;
		}

		ctx->len += bytes_read;
		if ((ctx->len + COPY_PIPE_BLOCK_SIZE) > ctx->size) {
			ctx->size *= 2;
			ctx->buf = z_realloc(ctx->buf, ctx->size + 1);
			memset(&ctx->buf[ctx->len], 0, (ctx->size + 1) - ctx->len);
		}
	}
}

static enum copy_pipe_result
copy_pipes(struct run_cmd_ctx *ctx)
{
	enum copy_pipe_result res;

	if ((res = copy_pipe(ctx->pipe_out.pipe[0], &ctx->out)) == copy_pipe_result_failed) {
		return res;
	}

	switch (copy_pipe(ctx->pipe_err.pipe[0], &ctx->err)) {
	case copy_pipe_result_waiting:
		return copy_pipe_result_waiting;
	case copy_pipe_result_finished:
		return res;
	case copy_pipe_result_failed:
		return copy_pipe_result_failed;
	default:
		assert(false && "unreachable");
		return copy_pipe_result_failed;
	}
}

static void
run_cmd_ctx_close_pipes(struct run_cmd_ctx *ctx)
{
	CLOSE_PIPE(ctx->pipe_err.pipe[0]);
	CLOSE_PIPE(ctx->pipe_err.pipe[1]);
	CLOSE_PIPE(ctx->pipe_out.pipe[0]);
	CLOSE_PIPE(ctx->pipe_out.pipe[1]);

	// TODO stdin
#if 0
	if (ctx->input_fd_open && close(ctx->input_fd) == -1) {
		LOG_E("failed to close: %s", win32_error());
	}
	ctx->input_fd_open = false;
#endif
}

enum run_cmd_state
run_cmd_collect(struct run_cmd_ctx *ctx)
{
	enum copy_pipe_result pipe_res = 0;

	while (true) {
		DWORD res;
		DWORD status;

		if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
			if ((pipe_res = copy_pipes(ctx)) == copy_pipe_result_failed) {
				return run_cmd_error;
			}
		}

		if (ctx->flags & run_cmd_ctx_flag_async) {
		} else {
			res = WaitForSingleObject(ctx->process, INFINITE);
			switch (res) {
			case WAIT_OBJECT_0:
				break;
			default:
				ctx->err_msg = "child exited abnormally";
				return run_cmd_error;
			}

			if (!GetExitCodeProcess(ctx->process, &status)) {
				ctx->err_msg = "can not get process exit code";
				return run_cmd_error;
			}

			ctx->status = (int)status;

			break;
		}
	}

	if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
		while (pipe_res != copy_pipe_result_finished) {
			if ((pipe_res = copy_pipes(ctx)) == copy_pipe_result_failed) {
				return run_cmd_error;
			}
		}
	}

	run_cmd_ctx_close_pipes(ctx);

	return run_cmd_finished;
}

static bool
open_pipes(HANDLE *pipe, const char *name)
{
	SECURITY_ATTRIBUTES sa;
	BOOL connected;
	SBUF_manual(sbuf);
	int i;

	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	i = 0;
	do {
		sbuf_clear(&sbuf);
		sbuf_pushf(NULL, &sbuf, "%s%d", name, i);
		pipe[0] = CreateNamedPipe(sbuf.buf, PIPE_ACCESS_INBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1UL, BUF_SIZE_4k, BUF_SIZE_4k, 0UL, &sa);
		i++;
	} while (pipe[0] == INVALID_HANDLE_VALUE);

	if (pipe[0] == INVALID_HANDLE_VALUE) {
		LOG_E("failed to create read end of the pipe: %ld %s", GetLastError(), win32_error());
		return false;
	}

	pipe[1] = CreateFile(sbuf.buf, FILE_WRITE_DATA | SYNCHRONIZE, 0UL, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0UL);
	if (pipe[1] == INVALID_HANDLE_VALUE) {
		LOG_E("failed to create write end of the pipe: %s", win32_error());
		CloseHandle(pipe[0]);
		return false;
	}

	connected = ConnectNamedPipe(pipe[0], NULL);
	if (connected == 0 && GetLastError() != ERROR_PIPE_CONNECTED) {
		LOG_E("failed to connect to pipe: %s", win32_error());
		CloseHandle(pipe[1]);
		CloseHandle(pipe[0]);
		return false;
	}

	return true;
}

static bool
open_run_cmd_pipe(struct run_cmd_ctx *ctx)
{
	ctx->pipe_out.pipe[0] = INVALID_HANDLE_VALUE;
	ctx->pipe_out.pipe[1] = INVALID_HANDLE_VALUE;
	ctx->pipe_err.pipe[0] = INVALID_HANDLE_VALUE;
	ctx->pipe_err.pipe[1] = INVALID_HANDLE_VALUE;

	if (ctx->flags & run_cmd_ctx_flag_dont_capture) {
		return true;
	}

	if (!open_pipes(ctx->pipe_out.pipe, "\\\\.\\pipe\\run_cmd_pipe_out")) {
	      return false;
	} else if (!open_pipes(ctx->pipe_err.pipe, "\\\\.\\pipe\\run_cmd_pipe_err")) {
	      return false;
	}

	return true;
}

static bool
run_cmd_internal(struct run_cmd_ctx *ctx, char *command_line, const char *envstr, uint32_t envc)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	const char *p;
	SBUF_manual(cmd);
	BOOL res;

	LOG_E(" * %s : cmd line: %s", __func__, command_line);
	if (envstr) {
		const char *k;
		uint32_t i = 0;

		p = k = envstr;
		for (;; ++p) {
			if (!p[0]) {
				if (!k) {
					k = p + 1;
				} else {
					SetEnvironmentVariable(k, p + 1);
					k = NULL;
					if (++i >= envc) {
						break;
					}
				}
			}
		}
	}

	if (log_should_print(log_debug)) {
		LL("executing '%s': '%s'\n", cmd.buf, command_line);

		if (envstr) {
			const char *k;
			uint32_t i = 0;
			LL("env:");
			p = k = envstr;
			for (;; ++p) {
				if (!p[0]) {
					if (!k) {
						k = p + 1;
					} else {
						log_plain(" %s='%s'", k, p + 1);
						k = NULL;

						if (++i >= envc) {
							break;
						}
					}
				}
			}

			log_plain("\n");
		}
	}

	// TODO stdin
#if 0
	if (ctx->stdin_path) {
		ctx->input_fd = open(ctx->stdin_path, O_RDONLY);
		if (ctx->input_fd == -1) {
			LOG_E("failed to open %s: %s", ctx->stdin_path, strerror(errno));
			goto err;
		}

		ctx->input_fd_open = true;
	}
#endif

	if (!open_run_cmd_pipe(ctx)) {
		goto err;
	}

	ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = ctx->pipe_out.pipe[1];
		// FIXME stdin
		si.hStdInput  = NULL;
		si.hStdError  = ctx->pipe_err.pipe[1];
	}

	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

	if (ctx->chdir) {
		if (!fs_dir_exists(ctx->chdir)) {
			LOG_E("directory %s does not exist: %s", ctx->chdir, win32_error());
			exit(1);
		}
	}

	LOG_E(" * %s: $%s$ $%s$", __func__, cmd.buf, command_line);
	res = CreateProcess(NULL, command_line, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, ctx->chdir, &si, &pi);
	if (!res) {
		LOG_E("CreateProcess() failed: %s", win32_error());
		goto err;
	}

	CLOSE_PIPE(ctx->pipe_out.pipe[1]);
	CLOSE_PIPE(ctx->pipe_err.pipe[1]);

	ctx->process = pi.hProcess;
	//CloseHandle(pi.hThread);
	ResumeThread(pi.hThread);

	sbuf_destroy(&cmd);

	if (ctx->flags & run_cmd_ctx_flag_async) {
		return true;
	}

	return run_cmd_collect(ctx) == run_cmd_finished;
err:
	return false;
}

static char *
msys2_path(void)
{
	char *path = NULL;
	char *sub_key_name;
	LSTATUS s;
	HKEY key;
	DWORD sub_keys_count;
	DWORD sub_keys_length_max;
	DWORD i;

	s = RegOpenKeyEx(HKEY_CURRENT_USER, KEY_PATH, 0UL, KEY_READ, &key);
	if (s != ERROR_SUCCESS) {
		return NULL;
	}

	s = RegQueryInfoKey(key, NULL, NULL, NULL, &sub_keys_count, &sub_keys_length_max, NULL, NULL, NULL, NULL, NULL, NULL);
	if (s != ERROR_SUCCESS || sub_keys_count == 0) {
		goto close_key;
	}

	sub_key_name = malloc(sub_keys_length_max + 1);
	if (!sub_key_name) {
		goto close_key;
	}

	for (i = 0UL; i < sub_keys_count; i++) {
		DWORD sub_key_length = sub_keys_length_max + 1;

		s = RegEnumKeyEx(key, i, sub_key_name, &sub_key_length, NULL, NULL, NULL, NULL);
		if (s == ERROR_SUCCESS) {
			char sub_key_path[MAX_PATH];
			HKEY sub_key;

			printf("subkey: %s %d %d\n", sub_key_name, (int)sub_key_length, lstrlen(sub_key_name));
			memcpy(sub_key_path, KEY_PATH, sizeof(KEY_PATH) - 1);
			sub_key_path[sizeof(KEY_PATH) - 1] = '\\';
			memcpy(sub_key_path + sizeof(KEY_PATH), sub_key_name, sub_key_length + 1);
			printf("subpath: \"%s\"\n", sub_key_path);

			s = RegOpenKeyEx(HKEY_CURRENT_USER, sub_key_path, 0UL, KEY_READ, &sub_key);
			if (s == ERROR_SUCCESS) {
				char data_location[MAX_PATH];
				char data_name[MAX_PATH];
				DWORD type;
				DWORD length;
				LSTATUS s2;

				type = REG_SZ;
				length = sizeof(data_location);
				s = RegQueryValueEx(sub_key, "DisplayName", NULL, &type, (LPBYTE)data_name, &length);

				type = REG_SZ;
				length = sizeof(data_location);
				s2 = RegQueryValueEx(sub_key, "InstallLocation", NULL, &type, (LPBYTE)data_location, &length);

				RegCloseKey(sub_key);

				if (s == ERROR_SUCCESS && s2 == ERROR_SUCCESS && lstrcmp(data_name, "MSYS2 64bit") == 0) {
					if (SUCCEEDED(StringCchCat(data_location, sizeof(data_location), "\\usr\\bin"))) {
						path = strdup(data_location);
						break;
					}
				}
			}
		}
	}
	free(sub_key_name);

  close_key:
	RegCloseKey(key);
	return path;
}

static bool
file_is_exe(const char *path)
{
	char buf[32767];

	if (fs_exe_exists(path)) {
		return true;
	}

	if (FAILED(StringCchCopy(buf, sizeof(buf), path))) {
		return false;
	}

	if (FAILED(StringCchCat(buf, sizeof(buf), ".exe"))) {
		return false;
	}

	return fs_exe_exists(buf);
}

static bool
argv_to_command_line(struct run_cmd_ctx *ctx, struct source *src, const char *argstr, char *const *argtab, struct sbuf *cmd, struct sbuf *cmd_argv0)
{
	const char *argv0, *new_argv0 = NULL, *new_argv1 = NULL;

	if (argstr) {
		argv0 = argstr;
	} else {
		argv0 = argtab[0];
	}

	if (!argv0 || !*argv0) {
		return false;
	}

	sbuf_clear(cmd);
	sbuf_clear(cmd_argv0);
	sbuf_pushs(NULL, cmd_argv0, argv0);

	LOG_E("******* argv0 %s", argv0);
	LOG_E("******* cmd   %s", cmd_argv0->buf);

	if (!path_is_basename(cmd_argv0->buf)) {
		path_make_absolute(NULL, cmd_argv0, argv0);

		if (file_is_exe(cmd_argv0->buf)) {
			/*
			 * surround the application with quotes,
			 * as it can contain spaces
			 */
			sbuf_push(NULL, cmd, '\"');
			sbuf_pushs(NULL, cmd, cmd_argv0->buf);
			sbuf_push(NULL, cmd, '\"');
		}
		else if (fs_has_extension(cmd_argv0->buf, ".bat")) {
			/*
			 * to run .bat file, run it with cmd.exe /c
			 */
			sbuf_pushs(NULL, cmd, "\"c:\\windows\\system32\\cmd.exe\" \"/c\" \"");
			sbuf_pushs(NULL, cmd, cmd_argv0->buf);
			sbuf_push(NULL, cmd, '\"');
		}
		else {
			if (!fs_read_entire_file(cmd_argv0->buf, src)) {
				ctx->err_msg = "error determining command interpreter";
				return false;
			}

			char *nl;
			if (!(nl = strchr(src->src, '\n'))) {
				ctx->err_msg = "error determining command interpreter: no newline in file";
				return false;
			}

			*nl = 0;

			uint32_t line_len = strlen(src->src);
			if (!(line_len > 2 && src->src[0] == '#' && src->src[1] == '!')) {
				ctx->err_msg = "error determining command interpreter: missing #!";
				return false;
			}

			const char *p = &src->src[2];
			char *s;

			while (strchr(" \t", *p)) {
				++p;
			}

			new_argv0 = p;

			if ((s = strchr(p, ' '))) {
				*s = 0;
				while (strchr(" \t", *p)) {
					++p;
				}
				new_argv1 = s + 1;
			}

			/*
			 * ignore "/usr/bin/env on Windows, see
			 * https://github.com/mesonbuild/meson/blob/5a1d294b5e27cd77b1ca4ae5d403abd005e20ea9/mesonbuild/dependencies/base.py#L604
			 */
			if (strcmp(new_argv0, "/usr/bin/env") == 0) {
				path_copy(NULL, cmd_argv0, new_argv1);
			} else {
				path_copy(NULL, cmd_argv0, new_argv0);
			}
		}
	} else {

		if (!fs_find_cmd(NULL, cmd_argv0, argv0)) {
			ctx->err_msg = "command not found";
			return false;
		}
		/*
		 * surround the application with quotes,
		 * as it can contain spaces
		 */
		sbuf_push(NULL, cmd, '\"');
		sbuf_pushs(NULL, cmd, cmd_argv0->buf);
		sbuf_push(NULL, cmd, '\"');
	}

	LOG_E("$*$*$* : 0x%p '%s' '%s'", argstr, new_argv0 ? new_argv0 : "NULL", new_argv1 ? new_argv1:"NULL");

	if (new_argv0) {
		if (new_argv1) {
			sbuf_push(NULL, cmd, '\"');
			sbuf_pushs(NULL, cmd, new_argv1);
			sbuf_push(NULL, cmd, '\"');
			sbuf_push(NULL, cmd, ' ');
		}
		sbuf_push(NULL, cmd, '\"');
		sbuf_pushs(NULL, cmd, argv0);
		sbuf_push(NULL, cmd, '\"');
	}

	if (argstr) {
		LOG_E(" ** argstr !");
		const char *p = argstr;
		for (;; ++p) {
			if (!*p) {
				++p;
				if (!*p) {
					break;
				}
				LOG_E(" ** p : %s", p);
				sbuf_push(NULL, cmd, ' ');
				sbuf_push(NULL, cmd, '\"');
				const char *it;
				for (it = p; *it; it++) {
					if (*it == '\"') {
						sbuf_push(NULL, cmd, '\\');
					}
					sbuf_push(NULL, cmd, *it);
				}
				sbuf_push(NULL, cmd, '\"');
			}
		}
	} else {
		char *const *p = argtab;
		for (p = argtab + 1; *p; p++) {
			sbuf_push(NULL, cmd, ' ');
			sbuf_push(NULL, cmd, '\"');
			sbuf_pushs(NULL, cmd, *p);
			sbuf_push(NULL, cmd, '\"');
		}
	}
	LOG_E("last quote : '%s'", cmd->buf);

	return true;
}

bool
run_cmd_argv(struct run_cmd_ctx *ctx, char *const *argtab, const char *envstr, uint32_t envc)
{
	bool ret = false;
	struct source src = { 0 };

	SBUF_manual(cmd);
	SBUF_manual(cmd_argv0);
	if (!argv_to_command_line(ctx, &src, NULL, argtab, &cmd, &cmd_argv0)) {
		goto err;
	}

	ret = run_cmd_internal(ctx, cmd.buf, envstr, envc);

err:
	fs_source_destroy(&src);
	sbuf_destroy(&cmd_argv0);
	sbuf_destroy(&cmd);

	return ret;
}

bool
run_cmd(struct run_cmd_ctx *ctx, const char *argstr, uint32_t argc, const char *envstr, uint32_t envc)
{
	bool ret = false;
	struct source src = { 0 };

	SBUF_manual(cmd);
	SBUF_manual(cmd_argv0);
	if (!argv_to_command_line(ctx, &src, argstr, NULL, &cmd, &cmd_argv0)) {
		goto err;
	}

	ret = run_cmd_internal(ctx, cmd.buf, envstr, envc);

err:
	fs_source_destroy(&src);
	sbuf_destroy(&cmd_argv0);
	sbuf_destroy(&cmd);

	return ret;
}

void
run_cmd_ctx_destroy(struct run_cmd_ctx *ctx)
{
	run_cmd_ctx_close_pipes(ctx);

	if (ctx->out.size) {
		z_free(ctx->out.buf);
		ctx->out.size = 0;
	}

	if (ctx->err.size) {
		z_free(ctx->err.buf);
		ctx->err.size = 0;
	}
}

bool
run_cmd_kill(struct run_cmd_ctx *ctx, bool force)
{
	BOOL r;
	if (force) {
		r = TerminateProcess(ctx->process, -1);
	} else {
		// FIXME
		r = TerminateProcess(ctx->process, -1);
	}

	if (!r) {
		LOG_E("error killing process 0x%p: %s", ctx->process, win32_error());
		return false;
	}

	return true;
}

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

#define CLOSE_PIPE(p_) do {                               \
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
 * [ ] run_cmd_argv()
 * [ ] run_cmd()
 * [X] run_cmd_ctx_destroy()
 * [ ] run_cmd_kill()
 */

static enum copy_pipe_result
copy_pipe(HANDLE pipe, OVERLAPPED *overlap, struct run_cmd_pipe_ctx *ctx)
{
	BOOL connected;
	BOOL pending;

	if (!ctx->size) {
		ctx->size = COPY_PIPE_BLOCK_SIZE;
		ctx->len = 0;
		ctx->buf = z_calloc(1, ctx->size + 1);
	}

	connected = ConnectNamedPipe(pipe, overlap);
	if (connected) {
		LOG_E("failed to connect to pipe: %s", win32_error());
		return copy_pipe_result_failed;
	}

	pending = FALSE;
	switch (GetLastError()) {
	case ERROR_IO_PENDING:
		pending = TRUE;
		break;
	case ERROR_PIPE_CONNECTED:
		if (SetEvent(overlap->hEvent))
			break;
	default:
		LOG_E("ConnectNamedPipe() failed: %s.", win32_error());
		pending = FALSE; /* already set above */
		break;
	}

	while (true) {
		DWORD bytes_read;
		BOOL res;

		res = ReadFile(pipe, &ctx->buf[ctx->len], ctx->size - ctx->len, &bytes_read, overlap);

		if (!res || !bytes_read) {
			if (GetLastError() == ERROR_BROKEN_PIPE) {
				return copy_pipe_result_finished;
			} else {
				return copy_pipe_result_failed;
			}
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

	if ((res = copy_pipe(ctx->pipe_out.pipe[0], &ctx->pipe_out.overlap, &ctx->out)) == copy_pipe_result_failed) {
		return res;
	}

	switch (copy_pipe(ctx->pipe_err.pipe[0], &ctx->pipe_err.overlap, &ctx->err)) {
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
	HANDLE pipe_tmp;
	HANDLE pipe_read;
	BOOL res;

	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	pipe_tmp = CreateNamedPipe(name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1UL, BUF_SIZE_4k, BUF_SIZE_4k, 0UL, &sa);
	if (pipe_tmp == INVALID_HANDLE_VALUE) {
		LOG_E("failed to create temporary read end of the pipe");
		return false;
	}

	pipe[1] = CreateFile(name, FILE_WRITE_DATA | SYNCHRONIZE, 0UL, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0UL);
	if (pipe[1] == INVALID_HANDLE_VALUE) {
		LOG_E("failed to create write end of the pipe");
		CloseHandle(pipe_tmp);
		return false;
	}

	res = DuplicateHandle(GetCurrentProcess(), pipe_tmp, GetCurrentProcess(), &pipe_read, 0UL, FALSE, DUPLICATE_SAME_ACCESS);
	if (!res) {
		LOG_E("failed to create read end of the pipe");
		CloseHandle(pipe[1]);
		pipe[1] = INVALID_HANDLE_VALUE;
		CloseHandle(pipe_tmp);
		return false;
	}

	pipe[0] = pipe_read;
	CloseHandle(pipe_tmp);

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

	ctx->pipe_out.overlap.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
	if (!ctx->pipe_out.overlap.hEvent) {
		LOG_E("failed to create output event: %s", win32_error());
		return false;
	}

	ctx->pipe_err.overlap.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
	if (!ctx->pipe_err.overlap.hEvent) {
		CloseHandle(ctx->pipe_err.overlap.hEvent);
		LOG_E("failed to create error event: %s", win32_error());
		return false;
	}

	if (!open_pipes(ctx->pipe_out.pipe, "\\\\.\\pipe\\run_cmd_pipe_out")) {
	      return false;
	} else if (!open_pipes(ctx->pipe_err.pipe, "\\\\.\\pipe\\run_cmd_pipe_err")) {
	      return false;
	}

	return true;
}

static bool
run_cmd_internal(struct run_cmd_ctx *ctx, const char *application_name, char *command_line, const char *envstr, uint32_t envc)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	const char *p;
	SBUF_manual(cmd);
	BOOL res;

	if (!fs_find_cmd(NULL, &cmd, application_name)) {
		ctx->err_msg = "command not found";
		return false;
	}

	if (log_should_print(log_debug)) {
		LL("executing %s: %s\n", application_name, command_line);

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

	ZeroMemory(&si,sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = (ctx->flags & run_cmd_ctx_flag_dont_capture) ? 0UL : STARTF_USESTDHANDLES;
	si.hStdOutput = ctx->pipe_out.pipe[1];
	// FIXME stdin
	si.hStdInput  = NULL;
	si.hStdError  = ctx->pipe_err.pipe[1];

	if (ctx->chdir) {
		if (!fs_dir_exists(ctx->chdir)) {
			LOG_E("directory %s does not exist: %s", ctx->chdir, win32_error());
			exit(1);
		}
	}

	res = CreateProcess(application_name, command_line, NULL, NULL, TRUE, CREATE_SUSPENDED, (LPVOID)envstr, ctx->chdir, &si, &pi);
	if (!res) {
		LOG_E("CreateProcess() failed");
		goto err;
	}

	CLOSE_PIPE(ctx->pipe_out.pipe[1]);
	CLOSE_PIPE(ctx->pipe_err.pipe[1]);

	ctx->process = pi.hProcess;
	CloseHandle(pi.hThread);
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

/* ext must have a dot, like ".bat" or ".sh" */
static bool
file_has_extension(const char *file, const char *ext)
{
	char *s;

	s = strrchr(file, '.');
	if (!s) {
		return false;
	}

	return lstrcmpi(s, ext) == 0;
}

static bool
argv_cat(char *cmd_line, size_t cmd_line_len, const char *arg)
{
	HRESULT res;

	res = StringCchCat(cmd_line, sizeof(cmd_line), " ");
	if (SUCCEEDED(res)) {
		res = StringCchCat(cmd_line, sizeof(cmd_line), arg);
		if (SUCCEEDED(res)) {
			return true;
		}
	}

	return false;
}

static bool
argv_to_command_line(struct run_cmd_ctx *ctx, const char *argstr, char *const *argtab, struct sbuf *cmd, char **application_name, char **command_line)
{
	char cmd_line[32767];
	const char *argv0;
	char *msys2_bindir = NULL;

	sbuf_clear(cmd);
	*command_line = NULL;
	*cmd_line = '\0';

	if (argstr) {
		argv0 = argstr;
	} else {
		argv0 = argtab[0];
	}

	if (!argv0 || !*argv0) {
		return false;
	}
	sbuf_pushs(NULL, cmd, argv0);

	/*
	* Reference:
	* https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa
	* Rationale:
	* - get the absolute path for argv0
	* - if it's a binary, we surround it with ", and we append the arguments
	* - if not:
	*   - if it's a .bat, then application name must be cmd.exe and command line must begin with /c follow with the .bat and its arguments
	*   - if it's a .sh, we must have a tty and the application name must be bash.exe and command line must begin with -c
	*/

	LOG_E("******* argv0 %s", argv0);
	path_make_absolute(NULL, cmd, argv0);
	LOG_E("******* argv0 path %s", cmd->buf);

	/* 1st: binary */
	if (fs_exe_exists(cmd->buf)) {
		*application_name = NULL;
		if (FAILED(StringCchPrintf(cmd_line, sizeof(cmd_line), "\"%s\"", cmd->buf))) {
			return false;
		}
	/* 2nd: batch file */
	} else if (file_has_extension(cmd->buf, ".bat")) {
		*application_name = _strdup("c:\\windows\\system32\\cmd.exe");
		if (!*application_name) {
			return false;
		}
		if (FAILED(StringCchPrintf(cmd_line, sizeof(cmd_line), "/c \"%s\"", cmd->buf))) {
			z_free(*application_name);
			*application_name = NULL;
			return false;
		}
	}
	/* 3rd: UNIX shell script */
	else if ((msys2_bindir = msys2_path()) != NULL) {
		char bash[MAX_PATH];

		if (FAILED(StringCchPrintf(bash, sizeof(bash), "\"%s\\bash.exe\"", msys2_bindir))) {
			z_free(*application_name);
			return false;
		}
		*application_name = _strdup(bash);
		if (!*application_name) {
			return false;
		}
		if (FAILED(StringCchPrintf(cmd_line, sizeof(cmd_line), "-c \"%s\"", cmd->buf))) {
			z_free(*application_name);
			*application_name = NULL;
			return false;
		}
	}
	LOG_E("******* 1  :  %s", *application_name);
	LOG_E(" * 1");

	if (argstr) {
          LOG_E(" * 1.1");
		const char *p = argstr;
		for (;; ++p) {
			if (!*p) {
				++p;
				if (!*p) {
					break;
				}
				if (!argv_cat(cmd_line, sizeof(cmd_line), p)) {
					z_free(*application_name);
					*application_name = NULL;
					return false;
				}
			}
		}
	} else {
	LOG_E(" * 1.2");
		char *const *p = argtab;
		for (p = argtab + 1; *p; p++) {
			if (!argv_cat(cmd_line, sizeof(cmd_line), *p)) {
				z_free(*application_name);
				*application_name = NULL;
				return false;
			}
		}
	}
	LOG_E(" * 2");

	*command_line = strdup(cmd_line);
	if (!*command_line) {
		z_free(*application_name);
		*application_name = NULL;
		return false;
	}
	LOG_E("******* %s -- %s", *application_name, *command_line);

	return true;
}

bool
run_cmd_argv(struct run_cmd_ctx *ctx, char *const *argtab, const char *envstr, uint32_t envc)
{
	char *command_line;
	char *application_name;
	bool ret = false;

	SBUF_manual(cmd);
	if (!argv_to_command_line(ctx, NULL, argtab, &cmd, &application_name, &command_line)) {
		return ret;
	}

	ret = run_cmd_internal(ctx, application_name, command_line, envstr, envc);
	sbuf_destroy(&cmd);

	return ret;
}

bool
run_cmd(struct run_cmd_ctx *ctx, const char *argstr, uint32_t argc, const char *envstr, uint32_t envc)
{
	char *application_name;
	char *command_line;
	bool ret = false;

	SBUF_manual(cmd);
	if (!argv_to_command_line(ctx, argstr, NULL, &cmd, &application_name, &command_line)) {
		return ret;
	}

	ret = run_cmd_internal(ctx, application_name, command_line, envstr, envc);
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

/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Vincent Torri <vtorri@outlook.fr>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include <windows.h>
#define STRSAFE_NO_CB_FUNCTIONS
#include <strsafe.h>

#include "args.h"
#include "buf_size.h"
#include "error.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "platform/timer.h"
#include "platform/windows/win32_error.h"

static uint32_t cnt_open;

#define record_handle(__h, v) _record_handle(__h, v, #__h)

static bool
_record_handle(HANDLE *h, HANDLE v, const char *desc)
{
	if (v == INVALID_HANDLE_VALUE) {
		return false;
	}

	++cnt_open;
	*h = v;
	return true;
}

#define close_handle(__h) _close_handle(__h, #__h)

static bool
_close_handle(HANDLE *h, const char *desc)
{
	if (*h == INVALID_HANDLE_VALUE) {
		return true;
	}

	assert(cnt_open);

	if (!CloseHandle(*h)) {
		LOG_E("failed to close handle %s:%p: %s", desc, *h, win32_error());
		return false;
	}

	--cnt_open;
	*h = INVALID_HANDLE_VALUE;
	return true;
}

enum copy_pipe_result {
	copy_pipe_result_finished,
	copy_pipe_result_waiting,
	copy_pipe_result_failed,
};

static enum copy_pipe_result
copy_pipe(struct win_pipe_inst *pipe, struct sbuf *sbuf)
{
	if (pipe->is_eof) {
		return copy_pipe_result_finished;
	}

	DWORD bytes_read;

	if (pipe->is_pending) {
		if (!GetOverlappedResult(pipe->handle, &pipe->overlapped, &bytes_read, TRUE)) {
			switch (GetLastError()) {
			case ERROR_BROKEN_PIPE:
				pipe->is_eof = true;
				if (!close_handle(&pipe->handle)) {
					return copy_pipe_result_failed;
				}
				return copy_pipe_result_finished;
			default:
				win32_fatal("GetOverlappedResult:");
				return copy_pipe_result_failed;
			}
		} else {
			if (bytes_read) {
				sbuf_pushn(0, sbuf, pipe->overlapped_buf, bytes_read);
				pipe->overlapped.Offset = 0;
				pipe->overlapped.OffsetHigh = 0;
			}

			ResetEvent(pipe->overlapped.hEvent);
		}
	}

	if (!ReadFile(pipe->handle,
		pipe->overlapped_buf,
		sizeof(pipe->overlapped_buf),
		&bytes_read,
		&pipe->overlapped)) {
		switch (GetLastError()) {
		case ERROR_BROKEN_PIPE:
			pipe->is_eof = true;
			if (!close_handle(&pipe->handle)) {
				return copy_pipe_result_failed;
			}
			return copy_pipe_result_finished;
		case ERROR_IO_PENDING:
			pipe->is_pending = true;
			break;
		default:
			win32_fatal("ReadFile:");
			return copy_pipe_result_failed;
		}
	} else {
		pipe->is_pending = false;
	}

	return copy_pipe_result_waiting;
}

static enum copy_pipe_result
copy_pipes(struct run_cmd_ctx *ctx)
{
	struct {
		struct win_pipe_inst *pipe;
		struct sbuf *sbuf;
	} pipes[2];
	HANDLE events[2];
	uint32_t event_count = 0;

#define PUSH_PIPE(__p, __sb) \
	if (!(__p)->is_eof) { \
		pipes[event_count].pipe = (__p); \
		pipes[event_count].sbuf = (__sb); \
		events[event_count] = (__p)->event; \
		++event_count; \
	} \

	PUSH_PIPE(&ctx->pipe_out, &ctx->out);
	PUSH_PIPE(&ctx->pipe_err, &ctx->err);

#undef PUSH_PIPE

	DWORD wait = WaitForMultipleObjects(event_count, events, FALSE, 0);

	if (wait == WAIT_TIMEOUT) {
		return copy_pipe_result_waiting;
	} else if (wait == WAIT_FAILED) {
		win32_fatal("WaitForMultipleObjects:");
	} else if (WAIT_ABANDONED_0 <= wait &&  wait < WAIT_ABANDONED_0 + event_count) {
		win32_fatal("WaitForMultipleObjects: abandoned");
	} else if (wait >= WAIT_OBJECT_0 + event_count) {
		win32_fatal("WaitForMultipleObjects: index out of range");
	} else {
		wait -= WAIT_OBJECT_0;
	}

	return copy_pipe(pipes[wait].pipe, pipes[wait].sbuf);
}

static void
run_cmd_ctx_close_pipes(struct run_cmd_ctx *ctx)
{
	if (!ctx->close_pipes) {
		return;
	}

	close_handle(&ctx->pipe_err.handle);
	close_handle(&ctx->pipe_err.event);
	close_handle(&ctx->pipe_out.handle);
	close_handle(&ctx->pipe_out.event);

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
	DWORD res;
	DWORD status;

	enum copy_pipe_result pipe_res = 0;

	bool loop = true;
	while (loop) {
		if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
			if ((pipe_res = copy_pipes(ctx)) == copy_pipe_result_failed) {
				return run_cmd_error;
			}
		}

		res = WaitForSingleObject(ctx->process, 1);
		switch (res) {
		case WAIT_TIMEOUT:
			if (ctx->flags & run_cmd_ctx_flag_async) {
				return run_cmd_running;
			}
			break;
		case WAIT_OBJECT_0:
			// State is signalled
			loop = false;
			break;
		case WAIT_FAILED:
			ctx->err_msg = win32_error();
			return run_cmd_error;
		case WAIT_ABANDONED:
			ctx->err_msg = "child exited abnormally (WAIT_ABANDONED)";
			return run_cmd_error;
		}
	}

	if (!GetExitCodeProcess(ctx->process, &status)) {
		ctx->err_msg = "can not get process exit code";
		return run_cmd_error;
	}

	ctx->status = (int)status;

	if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
		while (!(ctx->pipe_out.is_eof && ctx->pipe_err.is_eof)) {
			if (copy_pipes(ctx) == copy_pipe_result_failed) {
				return run_cmd_error;
			}
		}
	}

	return run_cmd_finished;
}

static bool
open_pipes(struct run_cmd_ctx *ctx, struct win_pipe_inst *pipe, const char *name)
{
	static uint64_t uniq = 0;
	char pipe_name[256];
	snprintf(pipe_name, ARRAY_LEN(pipe_name),
		"\\\\.\\pipe\\muon_run_cmd_pid%lu_%llu_%s", GetCurrentProcessId(), uniq, name);
	++uniq;

	if (!record_handle(&pipe->event, CreateEvent(
		NULL, // default security attribute
		TRUE, // manual-reset event
		TRUE, // initial state = signaled
		NULL  // unnamed event object
		))) {
		win32_fatal("CreateEvent:");
	}

	memset(&pipe->overlapped, 0, sizeof(pipe->overlapped));
	pipe->overlapped.hEvent = pipe->event;

	if (!record_handle(&pipe->handle, CreateNamedPipeA(
		pipe_name,
		PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_WAIT,
		PIPE_UNLIMITED_INSTANCES,
		0, 0, INFINITE, NULL
		))) {
		win32_fatal("CreateNamedPipe:");
		return false;
	}

	if (!ConnectNamedPipe(pipe->handle, &pipe->overlapped) && GetLastError() != ERROR_IO_PENDING) {
		win32_fatal("ConnectNamedPipe:");
		return false;
	}

	HANDLE output_write_child;
	{
		// Get the write end of the pipe as a handle inheritable across processes.
		HANDLE output_write_handle, dup;
		if (!record_handle(&output_write_handle,
			CreateFileA(pipe_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL))) {
			win32_fatal("CreateFile:");
			return false;
		}

		if (!DuplicateHandle(
			GetCurrentProcess(),
			output_write_handle,
			GetCurrentProcess(),
			&dup,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS)) {
			win32_fatal("DuplicateHandle:");
			return false;
		}

		if (!record_handle(&output_write_child, dup)) {
			return false;
		} else if (!close_handle(&output_write_handle)) {
			return false;
		}
	}

	pipe->child_handle = output_write_child;
	pipe->is_pending = true;
	return true;
}

static bool
open_run_cmd_pipe(struct run_cmd_ctx *ctx)
{
	if (ctx->flags & run_cmd_ctx_flag_dont_capture) {
		return true;
	}

	sbuf_init(&ctx->out, 0, 0, sbuf_flag_overflow_alloc);
	sbuf_init(&ctx->err, 0, 0, sbuf_flag_overflow_alloc);

	if (!open_pipes(ctx, &ctx->pipe_out, "out")) {
		return false;
	} else if (!open_pipes(ctx, &ctx->pipe_err, "err")) {
		return false;
	}

	ctx->close_pipes = true;

	return true;
}

static bool
run_cmd_internal(struct run_cmd_ctx *ctx, char *command_line, const char *envstr, uint32_t envc)
{
	const char *p;
	BOOL res;

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
		LL("executing : '%s'\n", command_line);

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

	SECURITY_ATTRIBUTES security_attributes;
	memset(&security_attributes, 0, sizeof(SECURITY_ATTRIBUTES));
	security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	security_attributes.bInheritHandle = TRUE;

	// Must be inheritable so subprocesses can dup to children.
	// TODO: delete when stdin support added
	HANDLE nul;
	if (!record_handle(&nul, CreateFileA("NUL", GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		&security_attributes, OPEN_EXISTING, 0, NULL))) {
		error_unrecoverable("couldn't open nul");
	}

	STARTUPINFOA startup_info;
	memset(&startup_info, 0, sizeof(startup_info));
	startup_info.cb = sizeof(STARTUPINFO);
	if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
		startup_info.dwFlags = STARTF_USESTDHANDLES;
		startup_info.hStdInput = nul;
		startup_info.hStdOutput = ctx->pipe_out.child_handle;
		startup_info.hStdError = ctx->pipe_err.child_handle;
	}

	PROCESS_INFORMATION process_info;
	memset(&process_info, 0, sizeof(process_info));

	if (ctx->chdir) {
		if (!fs_dir_exists(ctx->chdir)) {
			LOG_E("directory %s does not exist: %s", ctx->chdir, win32_error());
			exit(1);
		}
	}

	DWORD process_flags = 0;

	res = CreateProcess(NULL, command_line, NULL, NULL,
		/* inherit handles */ TRUE, process_flags,
		NULL, ctx->chdir,
		&startup_info, &process_info);
	if (!res) {
		DWORD error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND) {
		}
		LOG_E("CreateProcess() failed: %s", win32_error());
		goto err;
	}

	close_handle(&ctx->pipe_out.child_handle);
	close_handle(&ctx->pipe_err.child_handle);
	close_handle(&nul);

	record_handle(&ctx->process, process_info.hProcess);
	CloseHandle(process_info.hThread);

	if (ctx->flags & run_cmd_ctx_flag_async) {
		return true;
	}

	return run_cmd_collect(ctx) == run_cmd_finished;
err:
	return false;
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
		} else if (fs_has_extension(cmd_argv0->buf, ".bat")) {
			/*
			 * to run .bat file, run it with cmd.exe /c
			 */
			sbuf_pushs(NULL, cmd, "\"c:\\windows\\system32\\cmd.exe\" \"/c\" \"");
			sbuf_pushs(NULL, cmd, cmd_argv0->buf);
			sbuf_push(NULL, cmd, '\"');
		} else if (fs_exists(cmd_argv0->buf)) {
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
		const char *p = argstr;
		for (;; ++p) {
			if (!*p) {
				++p;
				if (!*p) {
					break;
				}
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

	return true;
}

bool
run_cmd_unsplit(struct run_cmd_ctx *ctx, char *cmd, const char *envstr, uint32_t envc)
{
	return run_cmd_internal(ctx, cmd, envstr, envc);
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
	close_handle(&ctx->process);
	run_cmd_ctx_close_pipes(ctx);

	sbuf_destroy(&ctx->out);
	sbuf_destroy(&ctx->err);
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

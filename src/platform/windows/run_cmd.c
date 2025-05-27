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
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"
#include "platform/timer.h"
#include "platform/windows/win32_error.h"

#define record_handle(__h, v) _record_handle(ctx, __h, v, #__h)

static bool
_record_handle(struct run_cmd_ctx *ctx, HANDLE *h, HANDLE v, const char *desc)
{
	if (!v || v == INVALID_HANDLE_VALUE) {
		return false;
	}

	++ctx->cnt_open;
	*h = v;
	return true;
}

#define close_handle(__h) _close_handle(ctx, __h, #__h)

static bool
_close_handle(struct run_cmd_ctx *ctx, HANDLE *h, const char *desc)
{
	if (!*h || *h == INVALID_HANDLE_VALUE) {
		return true;
	}

	assert(ctx->cnt_open);

	if (!CloseHandle(*h)) {
		LOG_E("failed to close handle %s:%p: %s", desc, *h, win32_error());
		return false;
	}

	--ctx->cnt_open;
	*h = INVALID_HANDLE_VALUE;
	return true;
}

enum copy_pipe_result {
	copy_pipe_result_finished,
	copy_pipe_result_waiting,
	copy_pipe_result_failed,
};

static enum copy_pipe_result
copy_pipe(struct run_cmd_ctx *ctx, struct win_pipe_inst *pipe, struct tstr *tstr)
{
	if (pipe->is_eof) {
		return copy_pipe_result_finished;
	}

	DWORD bytes_read;

	if (!GetOverlappedResult(pipe->handle, &pipe->overlapped, &bytes_read, TRUE)) {
		if (GetLastError() == ERROR_BROKEN_PIPE) {
			pipe->is_eof = true;
			if (!close_handle(&pipe->handle)) {
				return copy_pipe_result_failed;
			}
			return copy_pipe_result_finished;
		}

		win32_fatal("GetOverlappedResult:");
	}

	if (pipe->is_reading && bytes_read) {
		tstr_pushn(0, tstr, pipe->overlapped_buf, bytes_read);
	}
	memset(&pipe->overlapped, 0, sizeof(pipe->overlapped));
	pipe->is_reading = true;

	if (!ReadFile(
		    pipe->handle, pipe->overlapped_buf, sizeof(pipe->overlapped_buf), &bytes_read, &pipe->overlapped)) {
		if (GetLastError() == ERROR_BROKEN_PIPE) {
			pipe->is_eof = true;
			if (!close_handle(&pipe->handle)) {
				return copy_pipe_result_failed;
			}
			return copy_pipe_result_finished;
		}
		if (GetLastError() != ERROR_IO_PENDING) {
			win32_fatal("ReadFile:");
		}
	}

	return copy_pipe_result_waiting;
}

static enum copy_pipe_result
copy_pipes(struct run_cmd_ctx *ctx)
{
	DWORD bytes_read;
	struct win_pipe_inst *pipe;
	OVERLAPPED *overlapped;

	if (!GetQueuedCompletionStatus(ctx->ioport, &bytes_read, (PULONG_PTR)&pipe, &overlapped, 100)) {
		if (GetLastError() == WAIT_TIMEOUT) {
			return copy_pipe_result_waiting;
		} else if (GetLastError() != ERROR_BROKEN_PIPE) {
			win32_fatal("GetQueuedCompletionStatus:");
		}
	}

	struct tstr *tstr = 0;
	if (pipe == &ctx->pipe_out) {
		tstr = &ctx->out;
	} else if (pipe == &ctx->pipe_err) {
		tstr = &ctx->err;
	} else {
		/* return copy_pipe_result_waiting; */
		UNREACHABLE;
	}

	return copy_pipe(ctx, pipe, tstr);
}

static void
run_cmd_ctx_close_pipes(struct run_cmd_ctx *ctx)
{
	if (!ctx->close_pipes) {
		return;
	}

	close_handle(&ctx->pipe_err.handle);
	close_handle(&ctx->pipe_out.handle);
	close_handle(&ctx->ioport);

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
		if (!(ctx->flags & run_cmd_ctx_flag_dont_capture) && !(ctx->pipe_out.is_eof && ctx->pipe_err.is_eof)) {
			if ((pipe_res = copy_pipes(ctx)) == copy_pipe_result_failed) {
				return run_cmd_error;
			}
		}

		res = WaitForSingleObject(ctx->process, 0);
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
		case WAIT_FAILED: ctx->err_msg = win32_error(); return run_cmd_error;
		case WAIT_ABANDONED: ctx->err_msg = "child exited abnormally (WAIT_ABANDONED)"; return run_cmd_error;
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
	snprintf(pipe_name,
		ARRAY_LEN(pipe_name),
		"\\\\.\\pipe\\muon_run_cmd_pid%lu_%llu_%s",
		GetCurrentProcessId(),
		uniq,
		name);
	++uniq;

	memset(&pipe->overlapped, 0, sizeof(pipe->overlapped));

	if (!record_handle(&pipe->handle,
		    CreateNamedPipeA(pipe_name,
			    PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
			    PIPE_TYPE_BYTE | PIPE_WAIT,
			    PIPE_UNLIMITED_INSTANCES,
			    0,
			    0,
			    INFINITE,
			    NULL))) {
		win32_fatal("CreateNamedPipe:");
		return false;
	}

	if (!CreateIoCompletionPort(pipe->handle, ctx->ioport, (ULONG_PTR)pipe, 0)) {
		win32_fatal("CreateIoCompletionPort");
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

		if (!DuplicateHandle(GetCurrentProcess(),
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
	return true;
}

static bool
open_run_cmd_pipe(struct run_cmd_ctx *ctx)
{
	if (ctx->flags & run_cmd_ctx_flag_dont_capture) {
		return true;
	}

	assert(ctx->ioport == 0);
	if (!record_handle(&ctx->ioport, CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1))) {
		win32_fatal("CreateIoCompletionPort:");
	}

	tstr_init(&ctx->out, 0, 0, tstr_flag_overflow_alloc);
	tstr_init(&ctx->err, 0, 0, tstr_flag_overflow_alloc);

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

	ctx->process = INVALID_HANDLE_VALUE;

	LL("executing: ");
	log_plain(log_debug, "%s\n", command_line);

	if (envstr) {
		LPSTR oldenv = GetEnvironmentStrings();

		const char *k;
		uint32_t i = 0;
		LL("env:");
		p = k = envstr;
		for (; envc; ++p) {
			if (!p[0]) {
				if (!k) {
					k = p + 1;
				} else {
					assert(*k);
					log_plain(log_debug, " %s='%s'", k, p + 1);

					if (!SetEnvironmentVariable(k, p + 1)) {
						LOG_E("failed to set environment var %s='%s': %s",
							k,
							p + 1,
							win32_error());
						FreeEnvironmentStrings(oldenv);
						return false;
					}

					k = NULL;

					if (++i >= envc) {
						break;
					}
				}
			}
		}

		log_plain(log_debug, "\n");

		LPSTR newenv = GetEnvironmentStrings();

		// Clear out current env based on newenv
		char *var;
		for (var = newenv; *var;) {
			size_t len = strlen(var);
			char *split = strchr(var, '=');
			*split = 0;
			SetEnvironmentVariable(var, 0);
			*split = '=';
			var += len + 1;
		}

		// Copy newenv into ctx->env
		tstr_init(&ctx->env, 0, 0, tstr_flag_overflow_alloc);
		tstr_pushn(0, &ctx->env, newenv, var - newenv);
		tstr_push(0, &ctx->env, 0);

		FreeEnvironmentStrings(newenv);

		// Reset env based on oldenv
		for (var = oldenv; *var;) {
			size_t len = strlen(var);
			char *split = strchr(var, '=');
			*split = 0;
			SetEnvironmentVariable(var, split + 1);
			*split = '=';
			var += len + 1;
		}

		FreeEnvironmentStrings(oldenv);
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
		return false;
	}

	SECURITY_ATTRIBUTES security_attributes;
	memset(&security_attributes, 0, sizeof(SECURITY_ATTRIBUTES));
	security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	security_attributes.bInheritHandle = TRUE;

	// Must be inheritable so subprocesses can dup to children.
	// TODO: delete when stdin support added
	HANDLE nul;
	if (!record_handle(&nul,
		    CreateFileA("NUL",
			    GENERIC_READ,
			    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			    &security_attributes,
			    OPEN_EXISTING,
			    0,
			    NULL))) {
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

	if (strlen(command_line) >= 32767) {
		LOG_E("command too long");
	}

	res = CreateProcessA(NULL,
		command_line,
		NULL,
		NULL,
		/* inherit handles */ TRUE,
		process_flags,
		ctx->env.buf,
		ctx->chdir,
		&startup_info,
		&process_info);

	if (!res) {
		DWORD error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND) {
		}

		LOG_E("CreateProcess() failed: %s", win32_error());
		ctx->err_msg = "failed to create process";
	}

	close_handle(&ctx->pipe_out.child_handle);
	close_handle(&ctx->pipe_err.child_handle);
	close_handle(&nul);

	if (!res) {
		return false;
	}

	record_handle(&ctx->process, process_info.hProcess);
	CloseHandle(process_info.hThread);

	if (ctx->flags & run_cmd_ctx_flag_async) {
		return true;
	}

	return run_cmd_collect(ctx) == run_cmd_finished;
}

static void
run_cmd_push_argv(struct tstr *cmd, struct tstr *arg_buf, const char *arg, bool first)
{
	tstr_clear(arg_buf);
	shell_escape_cmd(0, arg_buf, arg);
	tstr_pushf(0, cmd, "%s%s", first ? "" : " ", arg_buf->buf);
}

static void
run_cmd_push_arg(struct tstr *cmd, struct tstr *arg_buf, const char *arg)
{
	run_cmd_push_argv(cmd, arg_buf, arg, false);
}

static bool
run_cmd_push_arg0(struct run_cmd_ctx *ctx, struct tstr *cmd, struct tstr *arg_buf, const char *arg)
{
	TSTR_manual(found_cmd);
	if (!fs_find_cmd(0, &found_cmd, arg)) {
		ctx->err_msg = "command not found";
		tstr_destroy(&found_cmd);
		return false;
	}

	run_cmd_push_argv(cmd, arg_buf, found_cmd.buf, true);
	tstr_destroy(&found_cmd);
	return true;
}

static bool
argv_to_command_line(struct run_cmd_ctx *ctx,
	struct source *src,
	const char *argstr,
	char *const *argv,
	uint32_t argstr_argc,
	struct tstr *cmd)
{
	bool res = false;
	TSTR_manual(arg_buf);
	const char *argv0 = argstr ? argstr : argv[0];

	tstr_clear(cmd);

	bool have_arg0 = false;

	if (fs_has_extension(argv0, ".bat")) {
		if (!run_cmd_push_arg0(ctx, cmd, &arg_buf, "cmd.exe")) {
			goto ret;
		}
		run_cmd_push_arg(cmd, &arg_buf, "/c");
		run_cmd_push_arg(cmd, &arg_buf, argv0);
		have_arg0 = true;
	} else if (fs_exists(argv0)) {
		DWORD _binary_type;
		if (!GetBinaryType(argv0, &_binary_type)) {
			const char *new_argv0 = 0, *new_argv1 = 0;
			if (!run_cmd_determine_interpreter(src, argv0, &ctx->err_msg, &new_argv0, &new_argv1)) {
				return false;
			}

			/* ignore /usr/bin/env on Windows */
			if (strcmp(new_argv0, "/usr/bin/env") == 0 && new_argv1) {
				new_argv0 = new_argv1;
				new_argv1 = 0;
			}

			if (!run_cmd_push_arg0(ctx, cmd, &arg_buf, new_argv0)) {
				goto ret;
			}

			if (new_argv1) {
				run_cmd_push_arg(cmd, &arg_buf, new_argv1);
			}

			run_cmd_push_arg(cmd, &arg_buf, argv0);

			have_arg0 = true;
		}
	}

	if (!have_arg0) {
		if (!run_cmd_push_arg0(ctx, cmd, &arg_buf, argv0)) {
			goto ret;
		}
	}

	if (argstr) {
		const char *p, *arg;
		uint32_t i = 0;

		arg = p = argstr;
		for (;; ++p) {
			if (!p[0]) {
				if (i > 0) {
					run_cmd_push_arg(cmd, &arg_buf, arg);
				}

				if (++i >= argstr_argc) {
					break;
				}

				arg = p + 1;
			}
		}
	} else {
		uint32_t i;
		for (i = 1; argv[i]; ++i) {
			run_cmd_push_arg(cmd, &arg_buf, argv[i]);
		}
	}

	res = true;
ret:
	tstr_destroy(&arg_buf);
	return res;
}

bool
run_cmd_unsplit(struct run_cmd_ctx *ctx, char *cmd, const char *envstr, uint32_t envc)
{
	return run_cmd_internal(ctx, cmd, envstr, envc);
}

bool
run_cmd_argv(struct run_cmd_ctx *ctx, char *const *argv, const char *envstr, uint32_t envc)
{
	bool ret = false;
	struct source src = { 0 };

	TSTR_manual(cmd);
	if (!argv_to_command_line(ctx, &src, NULL, argv, 0, &cmd)) {
		goto err;
	}

	ret = run_cmd_internal(ctx, cmd.buf, envstr, envc);

err:
	fs_source_destroy(&src);
	tstr_destroy(&cmd);

	return ret;
}

bool
run_cmd(struct run_cmd_ctx *ctx, const char *argstr, uint32_t argc, const char *envstr, uint32_t envc)
{
	bool ret = false;
	struct source src = { 0 };

	TSTR_manual(cmd);
	if (!argv_to_command_line(ctx, &src, argstr, NULL, argc, &cmd)) {
		goto err;
	}

	ret = run_cmd_internal(ctx, cmd.buf, envstr, envc);

err:
	fs_source_destroy(&src);
	tstr_destroy(&cmd);

	return ret;
}

void
run_cmd_ctx_destroy(struct run_cmd_ctx *ctx)
{
	close_handle(&ctx->process);
	run_cmd_ctx_close_pipes(ctx);

	tstr_destroy(&ctx->out);
	tstr_destroy(&ctx->err);
	tstr_destroy(&ctx->env);

	assert(ctx->cnt_open == 0);
}

bool
run_cmd_kill(struct run_cmd_ctx *ctx, bool force)
{
	BOOL r;
	if (force) {
		r = TerminateProcess(ctx->process, 1);
	} else {
		// FIXME
		r = TerminateProcess(ctx->process, 1);
	}

	if (!r) {
		LOG_E("error killing process 0x%p: %s", ctx->process, win32_error());
		return false;
	}

	return true;
}

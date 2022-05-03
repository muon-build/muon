#include "posix.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "args.h"
#include "buf_size.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

extern char **environ;

#define COPY_PIPE_BLOCK_SIZE BUF_SIZE_1k

enum copy_pipe_result {
	copy_pipe_result_finished,
	copy_pipe_result_waiting,
	copy_pipe_result_failed,
};

static enum copy_pipe_result
copy_pipe(int pipe, struct run_cmd_pipe_ctx *ctx)
{
	ssize_t b;
	if (!ctx->size) {
		ctx->size = COPY_PIPE_BLOCK_SIZE;
		ctx->len = 0;
		ctx->buf = z_calloc(1, ctx->size + 1);
	}

	while (true) {
		b = read(pipe, &ctx->buf[ctx->len], ctx->size - ctx->len);

		if (b == -1) {
			if (errno == EAGAIN) {
				return copy_pipe_result_waiting;
			} else {
				return copy_pipe_result_failed;
			}
		} else if (b == 0) {
			return copy_pipe_result_finished;
		}

		ctx->len += b;
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

	if ((res = copy_pipe(ctx->pipefd_out[0], &ctx->out)) == copy_pipe_result_failed) {
		return res;
	}

	switch (copy_pipe(ctx->pipefd_err[0], &ctx->err)) {
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
run_cmd_ctx_close_fds(struct run_cmd_ctx *ctx)
{
	if (ctx->pipefd_err_open[0] && close(ctx->pipefd_err[0]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->pipefd_err_open[0]  = false;

	if (ctx->pipefd_err_open[1] && close(ctx->pipefd_err[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->pipefd_err_open[1] = false;

	if (ctx->pipefd_out_open[0] && close(ctx->pipefd_out[0]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->pipefd_out_open[0] = false;

	if (ctx->pipefd_out_open[1] && close(ctx->pipefd_out[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->pipefd_out_open[1] = false;

	if (ctx->input_fd_open && close(ctx->input_fd) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->input_fd_open = false;

}

enum run_cmd_state
run_cmd_collect(struct run_cmd_ctx *ctx)
{
	int status;
	int r;

	enum copy_pipe_result pipe_res = 0;

	while (true) {
		if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
			if ((pipe_res = copy_pipes(ctx)) == copy_pipe_result_failed) {
				return run_cmd_error;
			}
		}

		if ((r = waitpid(ctx->pid, &status, WNOHANG)) == -1) {
			return run_cmd_error;
		} else if (r == 0) {
			if (ctx->flags & run_cmd_ctx_flag_async) {
				return run_cmd_running;

			} else {
				// sleep here for 1ms to give the process some
				// time to complete
				struct timespec req = { .tv_nsec = 1000000, };
				nanosleep(&req, NULL);
			}
		} else {
			break;
		}
	}

	assert(r == ctx->pid);

	if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
		while (pipe_res != copy_pipe_result_finished) {
			if ((pipe_res = copy_pipes(ctx)) == copy_pipe_result_failed) {
				return run_cmd_error;
			}
		}
	}

	run_cmd_ctx_close_fds(ctx);

	if (WIFEXITED(status)) {
		ctx->status = WEXITSTATUS(status);
	} else {
		ctx->err_msg = "child exited abnormally";
		return run_cmd_error;
	}

	return run_cmd_finished;
}

static bool
open_run_cmd_pipe(int fds[2], bool fds_open[2])
{
	if (pipe(fds) == -1) {
		LOG_E("failed to create pipe: %s", strerror(errno));
		return false;
	}

	fds_open[0] = true;
	fds_open[1] = true;

	int flags;
	if ((flags = fcntl(fds[0], F_GETFL)) == -1) {
		LOG_E("failed to get pipe flags: %s", strerror(errno));
		return false;
	} else if (fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == -1) {
		LOG_E("failed to set pipe flag O_NONBLOCK: %s", strerror(errno));
		return false;
	}

	return true;
}

static bool
run_cmd_internal(struct run_cmd_ctx *ctx, const char *_cmd, char *const *argv, const char *envstr)
{
	const char *p, *cmd;

	L("%s", _cmd);
	if (!fs_find_cmd(_cmd, &cmd)) {
		ctx->err_msg = "command not found";
		return false;
	}

	if (log_should_print(log_debug)) {
		LL("executing %s:", cmd);
		char *const *ap;

		for (ap = argv; *ap; ++ap) {
			log_plain(" '%s'", *ap);
		}
		log_plain("\n");

		if (envstr) {
			const char *k;
			LL("env:");
			p = k = envstr;
			for (;; ++p) {
				if (!*p) {
					++p;
					if (!*p) {
						break;
					} else if (!k) {
						k = p;
					} else {
						log_plain(" %s='%s'", k, p);
						k = NULL;
					}
				}
			}

			log_plain("\n");
		}
	}

	if (ctx->stdin_path) {
		ctx->input_fd = open(ctx->stdin_path, O_RDONLY);
		if (ctx->input_fd == -1) {
			LOG_E("failed to open %s: %s", ctx->stdin_path, strerror(errno));
			goto err;
		}

		ctx->input_fd_open = true;
	}

	if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
		if (!open_run_cmd_pipe(ctx->pipefd_out, ctx->pipefd_out_open)) {
			goto err;
		} else if (!open_run_cmd_pipe(ctx->pipefd_err, ctx->pipefd_err_open)) {
			goto err;
		}
	}

	if ((ctx->pid = fork()) == -1) {
		goto err;
	} else if (ctx->pid == 0 /* child */) {
		if (ctx->chdir) {
			if (chdir(ctx->chdir) == -1) {
				LOG_E("failed to chdir to %s: %s", ctx->chdir, strerror(errno));
				exit(1);
			}
		}

		if (ctx->stdin_path) {
			if (dup2(ctx->input_fd, 0) == -1) {
				LOG_E("failed to dup stdin: %s", strerror(errno));
				exit(1);
			}
		}

		if (!(ctx->flags & run_cmd_ctx_flag_dont_capture)) {
			if (dup2(ctx->pipefd_out[1], 1) == -1) {
				LOG_E("failed to dup stdout: %s", strerror(errno));
				exit(1);
			}
			if (dup2(ctx->pipefd_err[1], 2) == -1) {
				LOG_E("failed to dup stderr: %s", strerror(errno));
				exit(1);
			}
		}

		if (envstr) {
			const char *k;
			p = k = envstr;
			for (;; ++p) {
				if (!*p) {
					++p;
					if (!*p) {
						break;
					} else if (!k) {
						k = p;
					} else {
						int err;
						if ((err = setenv(k, p, 1)) != 0) {
							LOG_E("failed to set environment var %s='%s': %s",
								k, p, strerror(err));
							exit(1);
						}
						k = NULL;
					}
				}
			}
		}

		if (execve(cmd, (char *const *)argv, environ) == -1) {
			LOG_E("%s: %s", cmd, strerror(errno));
			exit(1);
		}

		abort();
	}

	/* parent */
	if (ctx->pipefd_err_open[1] && close(ctx->pipefd_err[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->pipefd_err_open[1] = false;

	if (ctx->pipefd_out_open[1] && close(ctx->pipefd_out[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->pipefd_out_open[1] = false;

	if (ctx->flags & run_cmd_ctx_flag_async) {
		return true;
	}

	return run_cmd_collect(ctx) == run_cmd_finished;
err:
	return false;
}

static void
push_argv_single(const char **argv, uint32_t *len, uint32_t max, const char *arg)
{
	assert(*len < max && "too many arguments");
	argv[*len] = arg;
	++(*len);
}

static bool
build_argv(struct run_cmd_ctx *ctx, struct source *src,
	const char *argstr, char *const *old_argv,
	const char **cmd, const char ***argv)
{
	const char *p, *argv0, *arg, *new_argv0 = NULL, *new_argv1 = NULL;
	const char **new_argv;
	uint32_t argc = 0, argi = 0;

	if (argstr) {
		argv0 = p = argstr;
		for (;; ++p) {
			if (!*p) {
				++argc;
				++p;
				if (!*p) {
					break;
				}
			}
		}
	} else {
		argv0 = old_argv[0];
		for (; old_argv[argc]; ++argc) {
		}
	}

	assert(*argv0 && "argv0 cannot be empty");

	if (!path_is_basename(argv0)) {
		static char cmd_path[PATH_MAX];
		if (!path_make_absolute(cmd_path, PATH_MAX, argv0)) {
			return false;
		}

		if (fs_exe_exists(cmd_path)) {
			*cmd = cmd_path;
		} else {
			if (!fs_read_entire_file(cmd_path, src)) {
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

			*cmd = new_argv0 = p;

			if ((s = strchr(p, ' '))) {
				*s = 0;
				while (strchr(" \t", *p)) {
					++p;
				}
				p = (new_argv1 = s + 1);
			}

			argc += new_argv1 ? 2 : 1;
		}
	}

	if (!new_argv0 && old_argv) {
		*argv = NULL;
		return true;
	}

	new_argv = z_calloc(argc + 1, sizeof(const char *));
	argi = 0;
	if (new_argv0) {
		push_argv_single(new_argv, &argi, argc, new_argv0);
		if (new_argv1) {
			push_argv_single(new_argv, &argi, argc, new_argv1);
		}
	}

	if (argstr) {
		arg = p = argstr;
		for (;; ++p) {
			if (!*p) {
				push_argv_single(new_argv, &argi, argc, arg);
				arg = p + 1;
				if (!*arg) {
					break;
				}
			}
		}
	} else {
		uint32_t i;
		for (i = 0; old_argv[i]; ++i) {
			push_argv_single(new_argv, &argi, argc, old_argv[i]);
		}
	}

	*argv = new_argv;
	return true;
}

bool
run_cmd_argv(struct run_cmd_ctx *ctx, const char *_cmd, char *const *argv, const char *envstr)
{
	bool ret = false;
	struct source src = { 0 };
	const char **new_argv = NULL;
	const char *cmd = argv[0];

	if (!build_argv(ctx, &src, NULL, argv, &cmd, &new_argv)) {
		goto err;
	}

	if (new_argv) {
		argv = (char **)new_argv;
	}

	ret = run_cmd_internal(ctx, cmd, (char *const *)argv, envstr);
err:
	fs_source_destroy(&src);
	if (new_argv) {
		z_free((void *)new_argv);
	}

	return ret;
}

bool
run_cmd(struct run_cmd_ctx *ctx, const char *argstr, const char *envstr)
{
	bool ret = false;
	struct source src = { 0 };
	const char **argv = NULL;
	const char *cmd = argstr;
	if (!build_argv(ctx, &src, argstr, NULL, &cmd, &argv)) {
		goto err;
	}

	ret = run_cmd_internal(ctx, cmd, (char *const *)argv, envstr);
err:
	fs_source_destroy(&src);

	if (argv) {
		z_free((void *)argv);
	}

	return ret;
}

void
run_cmd_ctx_destroy(struct run_cmd_ctx *ctx)
{
	run_cmd_ctx_close_fds(ctx);

	if (ctx->out.size) {
		z_free(ctx->out.buf);
		ctx->out.size = 0;
	}

	if (ctx->err.size) {
		z_free(ctx->err.buf);
		ctx->err.size = 0;
	}
}

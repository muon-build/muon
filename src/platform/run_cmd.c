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

extern char *const *environ;

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

enum run_cmd_state
run_cmd_collect(struct run_cmd_ctx *ctx)
{
	int status;
	int r;
	enum copy_pipe_result pipe_res;

	while (true) {
		if ((pipe_res = copy_pipes(ctx)) == copy_pipe_result_failed) {
			return run_cmd_error;
		}

		if ((r = waitpid(ctx->pid, &status, WNOHANG)) == -1) {
			return run_cmd_error;
		} else if (r == 0) {
			if (ctx->async) {
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

	while (pipe_res != copy_pipe_result_finished) {
		if ((pipe_res = copy_pipes(ctx)) == copy_pipe_result_failed) {
			return run_cmd_error;
		}
	}

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
		log_plain("failed to create pipe: %s", strerror(errno));
		return false;
	}

	fds_open[0] = true;
	fds_open[1] = true;

	int flags;
	if ((flags = fcntl(fds[0], F_GETFL)) == -1) {
		log_plain("failed to get pipe flags: %s", strerror(errno));
		return false;
	} else if (fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == -1) {
		log_plain("failed to set pipe flag O_NONBLOCK: %s", strerror(errno));
		return false;
	}

	return true;
}

bool
run_cmd(struct run_cmd_ctx *ctx, const char *_cmd, const char *const argv[], char *const envp[])
{
	struct source src = { 0 };
	bool src_open = false;

	const char *cmd;
	if (!path_is_basename(_cmd)) {
		static char cmd_path[PATH_MAX];
		if (!path_make_absolute(cmd_path, PATH_MAX, _cmd)) {
			return false;
		}

		if (fs_exe_exists(cmd_path)) {
			cmd = cmd_path;
		} else {
			if (!fs_read_entire_file(cmd_path, &src)) {
				ctx->err_msg = "error determining command interpreter";
				return false;
			}
			src_open = true;

			char *nl;
			if (!(nl = strchr(src.src, '\n'))) {
				ctx->err_msg = "error determining command interpreter: no newline in file";
				goto err;
			}

			*nl = 0;

			uint32_t line_len = strlen(src.src);
			if (!(line_len > 2 && src.src[0] == '#' && src.src[1] == '!')) {
				ctx->err_msg = "error determining command interpreter: missing #!";
				goto err;
			}

			const char *p = &src.src[2];
			char *s;

			static const char *new_argv[MAX_ARGS + 1];
			uint32_t argi = 0;

			while ((s = strchr(p, ' '))) {
				*s = 0;
				if (*p) {
					push_argv_single(new_argv, &argi, MAX_ARGS, p);
				}
				p = s + 1;
			}

			push_argv_single(new_argv, &argi, MAX_ARGS, p);

			uint32_t i;
			for (i = 0; argv[i]; ++i) {
				push_argv_single(new_argv, &argi, MAX_ARGS, argv[i]);
			}

			push_argv_single(new_argv, &argi, MAX_ARGS, NULL);
			argv = new_argv;
			_cmd = argv[0];
		}
	}

	if (!fs_find_cmd(_cmd, &cmd)) {
		ctx->err_msg = "command not found";
		return false;
	}

	if (log_should_print(log_debug)) {
		LL("executing %s:", cmd);
		const char *const *ap;

		for (ap = argv; *ap; ++ap) {
			log_plain(" '%s'", *ap);
		}
		log_plain("\n");

		if (envp) {
			char *const *ap;

			LL("env:");
			for (ap = envp; *ap; ++ap) {
				log_plain(" '%s'", *ap);
			}
			log_plain("\n");
		}
	}

	if (!open_run_cmd_pipe(ctx->pipefd_out, ctx->pipefd_out_open)) {
		goto err;
	} else if (!open_run_cmd_pipe(ctx->pipefd_err, ctx->pipefd_err_open)) {
		goto err;
	}

	if ((ctx->pid = fork()) == -1) {
		goto err;
	} else if (ctx->pid == 0 /* child */) {
		if (ctx->chdir) {
			if (chdir(ctx->chdir) == -1) {
				log_plain("failed to chdir to %s: %s", ctx->chdir, strerror(errno));
				exit(1);
			}
		}

		if (dup2(ctx->pipefd_out[1], 1) == -1) {
			log_plain("failed to dup stdout: %s", strerror(errno));
			exit(1);
		}
		if (dup2(ctx->pipefd_err[1], 2) == -1) {
			log_plain("failed to dup stderr: %s", strerror(errno));
			exit(1);
		}

		if (envp) {
			char *const *ap;
			for (ap = envp; *ap; ++ap) {
				char *const k = *ap;
				char *v = strchr(k, '=');
				assert(v);
				*v = 0;
				++v;

				int err;
				if ((err = setenv(k, v, 1)) != 0) {
					log_plain("failed to set environment: %s", strerror(err));
					exit(1);
				}
			}
		}

		if (execve(cmd, (char *const *)argv, environ) == -1) {
			log_plain("%s: %s", cmd, strerror(errno));
			exit(1);
		}

		abort();
	}

	/* parent */
	if (src_open) {
		fs_source_destroy(&src);
		src_open = false;
	}

	if (ctx->pipefd_err_open[1] && close(ctx->pipefd_err[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->pipefd_err_open[1] = false;

	if (ctx->pipefd_out_open[1] && close(ctx->pipefd_out[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	ctx->pipefd_out_open[1] = false;

	if (ctx->async) {
		return true;
	}

	return run_cmd_collect(ctx) == run_cmd_finished;
err:
	if (src_open) {
		fs_source_destroy(&src);
	}
	return false;
}

void
run_cmd_ctx_destroy(struct run_cmd_ctx *ctx)
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

	if (ctx->out.size) {
		z_free(ctx->out.buf);
		ctx->out.size = 0;
	}

	if (ctx->err.size) {
		z_free(ctx->err.buf);
		ctx->err.size = 0;
	}
}

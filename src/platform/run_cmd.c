#include "posix.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "args.h"
#include "buf_size.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"
#include "platform/run_cmd.h"

extern char *const *environ;

static bool
copy_pipe(int pipe, char **dest, uint32_t *len)
{
	uint32_t block_size = 256;
	uint32_t size = block_size + 1, i = 0;
	ssize_t b;

	char *buf = z_calloc(1, size);

	while (true) {
		b = read(pipe, &buf[i], block_size);

		if (b == -1) {
			return false;
		} else if (b < block_size) {
			i += b;
			break;
		}

		i += block_size;
		block_size *= 2;
		size += block_size;
		buf = z_realloc(buf, size);
		memset(&buf[i], 0, size - i);
	}

	*dest = buf;
	*len = i;
	return true;
}

enum run_cmd_state
run_cmd_collect(struct run_cmd_ctx *ctx)
{
	int status;
	int flags = 0;
	int r;

	if (ctx->async) {
		flags |= WNOHANG;
	}

	if ((r = waitpid(ctx->pid, &status, flags)) == -1) {
		return run_cmd_error;
	} else if (r == 0) {
		return run_cmd_running;
	}

	assert(r == ctx->pid);

	if (WIFEXITED(status)) {
		ctx->status = WEXITSTATUS(status);

		if (!copy_pipe(ctx->pipefd_out[0], &ctx->out, &ctx->out_len)) {
			return run_cmd_error;
		}

		if (!copy_pipe(ctx->pipefd_err[0], &ctx->err, &ctx->err_len)) {
			return run_cmd_error;
		}
	} else {
		ctx->err_msg = "child exited abnormally";
		return run_cmd_error;
	}

	return run_cmd_finished;
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
				push_argv_single(new_argv, &argi, MAX_ARGS, p);
				p = s + 1;
			}

			push_argv_single(new_argv, &argi, MAX_ARGS, p);

			uint32_t i;
			for (i = 0; argv[i]; ++i) {
				push_argv_single(new_argv, &argi, MAX_ARGS, argv[i]);
			}

			push_argv_single(new_argv, &argi, MAX_ARGS, NULL);
			argv = new_argv;
			cmd = argv[0];
		}
	} else {
		if (!fs_find_cmd(_cmd, &cmd)) {
			ctx->err_msg = "command not found";
			return false;
		}
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

	if (pipe(ctx->pipefd_out) == -1) {
		goto err;
	}
	ctx->pipefd_out_open[0] = true;
	ctx->pipefd_out_open[1] = true;

	if (pipe(ctx->pipefd_err) == -1) {
		goto err;
	}
	ctx->pipefd_err_open[0] = true;
	ctx->pipefd_err_open[1] = true;

	if ((ctx->pid = fork()) == -1) {
		goto err;
	} else if (ctx->pid == 0 /* child */) {
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

	if (ctx->out) {
		z_free(ctx->out);
		ctx->out = NULL;
	}

	if (ctx->err) {
		z_free(ctx->err);
		ctx->err = NULL;
	}
}

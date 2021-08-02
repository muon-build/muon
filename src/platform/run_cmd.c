#include "posix.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "buf_size.h"
#include "log.h"
#include "platform/run_cmd.h"

bool
run_cmd(struct run_cmd_ctx *ctx, const char *cmd, char *const argv[])
{
	pid_t pid;
	bool res = false, pipefd_out_open[2] = { 0 }, pipefd_err_open[2] = { 0 };
	int pipefd_out[2] = { 0 }, pipefd_err[2] = { 0 };

	/* L("child: running %s", cmd); */
	/* char *const *ap; */
	/* for (ap = argv; *ap; ++ap) { */
	/* 	L("child: > arg '%s'", *ap); */
	/* } */

	static char out_buf[BUF_SIZE_1m] = { 0 };
	static char err_buf[BUF_SIZE_1m] = { 0 };

	memset(out_buf, 0, BUF_SIZE_1m);
	memset(err_buf, 0, BUF_SIZE_1m);

	if (pipe(pipefd_out) == -1) {
		ctx->err_no = errno;
		return false;
	}
	pipefd_out_open[0] = true;
	pipefd_out_open[1] = true;

	if (pipe(pipefd_err) == -1) {
		ctx->err_no = errno;
		goto err;
	}
	pipefd_err_open[0] = true;
	pipefd_err_open[1] = true;

	if ((pid = fork()) == -1) {
		ctx->err_no = errno;
		goto err;
	} else if (pid == 0 /* child */) {
		if (dup2(pipefd_out[1], 1) == -1) {
			L("child: failed to dup stdout: %s", strerror(errno));
			exit(1);
		}
		if (dup2(pipefd_err[1], 2) == -1) {
			L("child: failed to dup stderr: %s", strerror(errno));
			exit(1);
		}

		/* L("child: running %s", cmd); */
		/* char *const *ap; */
		/* for (ap = argv; *ap; ++ap) { */
		/* 	L("child: > arg '%s'", *ap); */
		/* } */

		if (execvp(cmd, argv) == -1) {
			exit(1);
		}

		exit(1); // unreachable
	}

	/* parent */
	if (pipefd_err_open[1] && close(pipefd_err[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	pipefd_err_open[1] = false;

	if (pipefd_out_open[1] && close(pipefd_out[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	pipefd_out_open[1] = false;

	int status;
	if (waitpid(pid, &status, 0) != pid) {
		ctx->err_no = errno;
		goto err;
	}

	if (WIFEXITED(status)) {
		ctx->status = WEXITSTATUS(status);

		read(pipefd_out[0], out_buf, BUF_SIZE_1m);
		read(pipefd_err[0], err_buf, BUF_SIZE_1m);

		ctx->out = out_buf;
		ctx->err = err_buf;
		res = true;
	} else {
		ctx->err_msg = "child exited abnormally";
	}
err:
	if (pipefd_err_open[0] && close(pipefd_err[0]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	if (pipefd_err_open[1] && close(pipefd_err[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	if (pipefd_out_open[0] && close(pipefd_out[0]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}
	if (pipefd_out_open[1] && close(pipefd_out[1]) == -1) {
		LOG_E("failed to close: %s", strerror(errno));
	}

	return res;
}

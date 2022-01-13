#ifndef MUON_PLATFORM_RUN_CMD_H
#define MUON_PLATFORM_RUN_CMD_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

enum run_cmd_state {
	run_cmd_error,
	run_cmd_running,
	run_cmd_finished,
};

struct run_cmd_pipe_ctx {
	size_t size;
	size_t len;
	char *buf;
};

enum run_cmd_ctx_flags {
	run_cmd_ctx_flag_async = 1 << 0,
	run_cmd_ctx_flag_dont_capture = 1 << 1,
};

struct run_cmd_ctx {
	struct run_cmd_pipe_ctx err, out;
	const char *err_msg; // set on error
	const char *chdir; // set by caller
	int pipefd_out[2], pipefd_err[2];
	int status;
	pid_t pid;

	bool pipefd_out_open[2], pipefd_err_open[2];
	enum run_cmd_ctx_flags flags;
};

bool run_cmd(struct run_cmd_ctx *ctx, const char *_cmd, const char *const argv[], char *const envp[]);
enum run_cmd_state run_cmd_collect(struct run_cmd_ctx *ctx);
void run_cmd_ctx_destroy(struct run_cmd_ctx *ctx);
#endif

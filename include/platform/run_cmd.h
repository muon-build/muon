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

struct run_cmd_ctx {
	bool async;
	bool pipefd_out_open[2], pipefd_err_open[2];
	int pipefd_out[2], pipefd_err[2];
	pid_t pid;

	char *err, *out;
	uint32_t err_len, out_len;
	int status;

	/* error handling */
	const char *err_msg;
};

bool run_cmd(struct run_cmd_ctx *ctx, const char *_cmd, const char *const argv[], char *const envp[]);
enum run_cmd_state run_cmd_collect(struct run_cmd_ctx *ctx);
void run_cmd_ctx_destroy(struct run_cmd_ctx *ctx);
#endif

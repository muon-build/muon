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
	const char *stdin_path; // set by caller
	int pipefd_out[2], pipefd_err[2];
	int input_fd;
	int status;
	pid_t pid;

	bool input_fd_open;
	bool pipefd_out_open[2], pipefd_err_open[2];
	enum run_cmd_ctx_flags flags;
};

/*
 * argstr is a NUL delimited array of strings
 * envstr is like argstr, every two strings is considered a key/value pair
 */
uint32_t argstr_to_argv(const char *argstr, uint32_t argc, const char *prepend, char *const **res);

bool run_cmd(struct run_cmd_ctx *ctx, const char *argstr, uint32_t argc, const char *envstr, uint32_t envc);
bool run_cmd_argv(struct run_cmd_ctx *ctx, const char *cmd, char *const *argv, const char *envstr, uint32_t envc);
enum run_cmd_state run_cmd_collect(struct run_cmd_ctx *ctx);
void run_cmd_ctx_destroy(struct run_cmd_ctx *ctx);
bool run_cmd_kill(struct run_cmd_ctx *ctx, bool force);
#endif

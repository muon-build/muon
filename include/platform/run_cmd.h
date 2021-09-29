#ifndef MUON_PLATFORM_RUN_CMD_H
#define MUON_PLATFORM_RUN_CMD_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

struct run_cmd_ctx {
	char *err, *out;
	uint32_t err_len, out_len;
	int status;

	/* error handling */
	const char *err_msg;
};

bool run_cmd(struct run_cmd_ctx *ctx, const char *_cmd, char *const argv[], char *const envp[]);
void run_cmd_ctx_destroy(struct run_cmd_ctx *ctx);
#endif

#ifndef BOSON_RUN_CMD_H
#define BOSON_RUN_CMD_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

struct run_cmd_ctx {
	char *err, *out;
	int status;

	/* error handling */
	const char *err_msg;
	int64_t err_no;
};

bool run_cmd(struct run_cmd_ctx *ctx, const char *cmd, char *const argv[]);
#endif

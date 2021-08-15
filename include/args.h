#ifndef MUON_ARGS_H
#define MUON_ARGS_H

#include "lang/workspace.h"

struct args {
	const char **args;
	uint32_t len;
};

void push_args(struct workspace *wk, uint32_t arr, const struct args *args);
void push_argv_single(const char **argv, uint32_t *len, uint32_t max, const char *arg);
void push_argv(const char **argv, uint32_t *len, uint32_t max, const struct args *args);

uint32_t join_args_shell(struct workspace *wk, uint32_t arr);
uint32_t join_args_ninja(struct workspace *wk, uint32_t arr);

bool arr_to_args(struct workspace *wk, uint32_t arr, uint32_t *res);
#endif

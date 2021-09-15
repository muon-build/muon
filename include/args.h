#ifndef MUON_ARGS_H
#define MUON_ARGS_H

#include "lang/workspace.h"

struct args {
	const char **args;
	uint32_t len;
};

bool ninja_escape(char *buf, uint32_t len, const char *str);

void push_args(struct workspace *wk, uint32_t arr, const struct args *args);
void push_argv_single(const char **argv, uint32_t *len, uint32_t max, const char *arg);
void push_argv(const char **argv, uint32_t *len, uint32_t max, const struct args *args);

uint32_t join_args_plain(struct workspace *wk, uint32_t arr);
uint32_t join_args_shell(struct workspace *wk, uint32_t arr);
uint32_t join_args_ninja(struct workspace *wk, uint32_t arr);
bool join_args_argv(struct workspace *wk, char **argv, uint32_t len, uint32_t arr);

bool arr_to_args(struct workspace *wk, uint32_t arr, uint32_t *res);

enum env_to_envp_flags {
	env_to_envp_flag_subdir    = 1 << 0,
	env_to_envp_flag_dist_root = 1 << 1,
};

bool env_to_envp(struct workspace *wk, uint32_t err_node, char *const *ret[], obj val, enum env_to_envp_flags flags);
#endif

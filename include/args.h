#ifndef MUON_ARGS_H
#define MUON_ARGS_H

#include "lang/workspace.h"

struct args {
	const char **args;
	uint32_t len;
};

bool shell_escape(char *buf, uint32_t len, const char *str);
bool ninja_escape(char *buf, uint32_t len, const char *str);

void push_args(struct workspace *wk, obj arr, const struct args *args);
void push_args_null_terminated(struct workspace *wk, obj arr, char *const *argv);
void push_argv_single(const char **argv, uint32_t *len, uint32_t max, const char *arg);
void push_argv(const char **argv, uint32_t *len, uint32_t max, const struct args *args);

obj join_args_plain(struct workspace *wk, obj arr);
obj join_args_shell(struct workspace *wk, obj arr);
obj join_args_ninja(struct workspace *wk, obj arr);
obj join_args_shell_ninja(struct workspace *wk, obj arr);
bool join_args_argv(struct workspace *wk, const char **argv, uint32_t len, obj arr);

enum arr_to_args_flags {
	arr_to_args_build_target = 1 << 0,
	arr_to_args_custom_target = 1 << 1,
	arr_to_args_external_program = 1 << 2,
	arr_to_args_alias_target = 1 << 3,
	arr_to_args_relativize_paths = 1 << 4,
};

bool arr_to_args(struct workspace *wk, enum arr_to_args_flags mode, obj arr, obj *res);

enum env_to_envp_flags {
	env_to_envp_flag_subdir    = 1 << 0,
	env_to_envp_flag_dist_root = 1 << 1,
};

bool env_to_envp(struct workspace *wk, uint32_t err_node, char *const *ret[], obj val, enum env_to_envp_flags flags);
#endif

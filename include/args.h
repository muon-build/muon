#ifndef MUON_ARGS_H
#define MUON_ARGS_H

#include "lang/workspace.h"

struct args {
	const char **args;
	uint32_t len;
};

bool shell_escape(char *buf, uint32_t len, const char *str);
bool ninja_escape(char *buf, uint32_t len, const char *str);
bool pkgconf_escape(char *buf, uint32_t len, const char *str);

void push_args(struct workspace *wk, obj arr, const struct args *args);
void push_args_null_terminated(struct workspace *wk, obj arr, char *const *argv);

obj join_args_plain(struct workspace *wk, obj arr);
obj join_args_shell(struct workspace *wk, obj arr);
obj join_args_ninja(struct workspace *wk, obj arr);
obj join_args_shell_ninja(struct workspace *wk, obj arr);
obj join_args_pkgconf(struct workspace *wk, obj arr);

enum arr_to_args_flags {
	arr_to_args_build_target = 1 << 0,
	arr_to_args_custom_target = 1 << 1,
	arr_to_args_external_program = 1 << 2,
	arr_to_args_alias_target = 1 << 3,
	arr_to_args_relativize_paths = 1 << 4,
};

bool arr_to_args(struct workspace *wk, enum arr_to_args_flags mode, obj arr, obj *res);

void join_args_argstr(struct workspace *wk, const char **res, obj arr);
void env_to_envstr(struct workspace *wk, const char **res, obj val);
#endif

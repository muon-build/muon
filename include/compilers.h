#ifndef COMPILERS_H
#define COMPILERS_H

#include "lang/workspace.h"

enum compiler_type {
	compiler_gcc,
	compiler_clang,
	compiler_type_count,
};

enum compiler_language {
	compiler_language_c,
	compiler_language_c_hdr,
	compiler_language_cpp,
	compiler_language_cpp_hdr,
	compiler_language_count,
};

enum compiler_deps_type {
	compiler_deps_none,
	compiler_deps_gcc,
	compiler_deps_msvc,
};

enum compiler_optimization_lvl {
	compiler_optimization_lvl_0,
	compiler_optimization_lvl_1,
	compiler_optimization_lvl_2,
	compiler_optimization_lvl_3,
	compiler_optimization_lvl_g,
	compiler_optimization_lvl_s,
};

enum compiler_warning_lvl {
	compiler_warning_lvl_0,
	compiler_warning_lvl_1,
	compiler_warning_lvl_2,
	compiler_warning_lvl_3,
};

struct compiler_args {
	const char **args;
	uint32_t len;
};

typedef const struct compiler_args *((*compiler_get_arg_func_0)(void));
typedef const struct compiler_args *((*compiler_get_arg_func_1i)(uint32_t));
typedef const struct compiler_args *((*compiler_get_arg_func_1s)(const char *));
typedef const struct compiler_args *((*compiler_get_arg_func_2s)(const char *, const char *));

struct compiler {
	struct {
		compiler_get_arg_func_2s deps;
		compiler_get_arg_func_0 compile_only;
		compiler_get_arg_func_1s output;
		compiler_get_arg_func_1i optimization;
		compiler_get_arg_func_0 debug;
		compiler_get_arg_func_1i warning_lvl;
		compiler_get_arg_func_1s set_std;
		compiler_get_arg_func_1s include;
	} args;
	enum compiler_language lang;
	enum compiler_deps_type deps;
};

struct language {
	bool is_header;
};

extern const struct compiler compilers[];
extern const struct language languages[];

const char *compiler_type_to_s(enum compiler_type t);
const char *compiler_language_to_s(enum compiler_language l);
bool s_to_compiler_language(const char *s, enum compiler_language *l);
bool filename_to_compiler_language(const char *str, enum compiler_language *l);
bool compiler_detect(struct workspace *wk, uint32_t *comp, enum compiler_language);
#endif

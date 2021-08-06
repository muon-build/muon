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

struct compiler {
	const char *command,
		   *deps,
		   *depfile,
		   *description;
	enum compiler_language lang;
};

extern const struct compiler compilers[];

const char *compiler_type_to_s(enum compiler_type t);
const char *compiler_language_to_s(enum compiler_language l);
bool s_to_compiler_language(const char *s, enum compiler_language *l);
bool filename_to_compiler_language(const char *str, enum compiler_language *l);
bool compiler_detect(struct workspace *wk, uint32_t *comp, enum compiler_language);
#endif

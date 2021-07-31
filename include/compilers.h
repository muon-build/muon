#ifndef COMPILERS_H
#define COMPILERS_H

#include "workspace.h"

enum compiler_type {
	compiler_gcc,
	compiler_clang,
	compiler_type_count,
};

struct compiler {
	const char *command,
		   *deps,
		   *depfile,
		   *description;
};

extern const struct compiler compilers[];

bool compiler_detect_c(struct workspace *wk, uint32_t *comp);
#endif

#ifndef MUON_LANG_FMT_H
#define MUON_LANG_FMT_H

#include <stdio.h>

#include "lang/parser.h"

bool fmt(struct ast *ast, FILE *out);
#endif

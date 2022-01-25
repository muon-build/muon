#ifndef MUON_BACKEND_NINJA_ALIAS_TARGET_H
#define MUON_BACKEND_NINJA_ALIAS_TARGET_H

#include <stdio.h>

#include "lang/types.h"

struct workspace;
struct project;

bool ninja_write_alias_tgt(struct workspace *wk, obj tgt_id, FILE *out);

#endif

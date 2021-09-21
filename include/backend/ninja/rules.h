#ifndef MUON_BACKEND_NINJA_RULES_H
#define MUON_BACKEND_NINJA_RULES_H
#include "lang/workspace.h"

bool ninja_write_rules(FILE *out, struct workspace *wk, struct project *main_proj);
#endif

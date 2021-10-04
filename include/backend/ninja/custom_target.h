#ifndef MUON_BACKEND_NINJA_CUSTOM_TARGET_H
#define MUON_BACKEND_NINJA_CUSTOM_TARGET_H
#include "lang/workspace.h"

bool ninja_write_custom_tgt(struct workspace *wk, const struct project *proj, obj tgt_id, FILE *out);
#endif

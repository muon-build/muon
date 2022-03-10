#ifndef MUON_BACKEND_NINJA_BUILD_TARGET_H
#define MUON_BACKEND_NINJA_BUILD_TARGET_H
#include "lang/workspace.h"
struct write_tgt_ctx;

bool ninja_write_build_tgt(struct workspace *wk, obj tgt_id, struct write_tgt_ctx *ctx);
#endif

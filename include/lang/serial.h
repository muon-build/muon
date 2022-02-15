#ifndef MUON_LANG_SERIAL_H
#define MUON_LANG_SERIAL_H

#include "lang/workspace.h"

bool serial_dump(struct workspace *wk_src, obj o, FILE *f);
bool serial_load(struct workspace *wk, obj *res, FILE *f);

#endif

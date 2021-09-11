#ifndef MUON_LANG_SERIAL_H
#define MUON_LANG_SERIAL_H

#include "lang/workspace.h"

bool serial_dump(struct workspace *wk, uint32_t obj, FILE *f);
bool serial_load(struct workspace *wk, uint32_t *obj, FILE *f);

#endif

#ifndef MUON_MACHINE_FILE_H
#define MUON_MACHINE_FILE_H

#include "lang/workspace.h"

bool machine_file_parse(struct workspace *dest_wk, const char *path);
#endif

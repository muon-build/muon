#ifndef MUON_LANG_ANALYZE_H
#define MUON_LANG_ANALYZE_H

#include "workspace.h"

struct analyze_opts {
	bool subdir_error,
	     unused_variable_error,
	     silence_warnings;
};

bool do_analyze(struct analyze_opts *opts);
#endif

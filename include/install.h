#ifndef MUON_INSTALL_H
#define MUON_INSTALL_H
#include <stdbool.h>

struct install_options {
	bool dry_run;
};

bool install_run(struct install_options *opts);
#endif

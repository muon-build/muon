#ifndef MUON_FUNCTIONS_DEFAULT_SETUP_H
#define MUON_FUNCTIONS_DEFAULT_SETUP_H
#include "functions/common.h"

enum func_setup_flag {
	func_setup_flag_force = 1 << 0,
	func_setup_flag_no_build = 1 << 1,
};

extern uint32_t func_setup_flags;

bool do_setup(struct workspace *wk);
bool func_setup(struct workspace *wk, uint32_t _, uint32_t args_node, uint32_t *obj);
#endif

#ifndef MUON_OPTIONS_H
#define MUON_OPTIONS_H
#include "lang/workspace.h"

struct option_override {
	// strings
	obj proj, name, val;
	enum option_value_source source;
	bool obj_value;
};

bool create_option(struct workspace *wk, uint32_t node, obj opts, obj opt, obj val);
bool get_option(struct workspace *wk, const struct project *proj, const struct str *name, obj *res);
void get_option_value(struct workspace *wk, const struct project *proj, const char *name, obj *res);

bool check_invalid_option_overrides(struct workspace *wk);
bool check_invalid_subproject_option(struct workspace *wk);

bool setup_project_options(struct workspace *wk, const char *cwd);
bool init_global_options(struct workspace *wk);

bool parse_and_set_cmdline_option(struct workspace *wk, char *lhs);
bool parse_and_set_default_options(struct workspace *wk, uint32_t err_node, obj arr, obj project_name, bool is_subproject);

enum wrap_mode {
	wrap_mode_nopromote,
	wrap_mode_nodownload,
	wrap_mode_nofallback,
	wrap_mode_forcefallback,
};
enum wrap_mode get_option_wrap_mode(struct workspace *wk);
#endif

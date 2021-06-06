#include "posix.h"

#include <getopt.h>
#include <stdbool.h>
#include <string.h>

#include "workspace.h"
#include "opts.h"
#include "log.h"

bool
parse_config_opt(struct workspace *wk, char *lhs)
{
	char *rhs = strchr(lhs, '='), *subproj;
	if (!rhs) {
		LOG_W(log_misc, "'%s' expected '='", lhs);
		return false;
	}
	*rhs = 0;
	++rhs;

	subproj = lhs;
	if ((lhs = strchr(lhs, ':'))) {
		*lhs = 0;
		++lhs;
	} else {
		lhs = subproj;
		subproj = NULL;
	}

	if (!*lhs) {
		LOG_W(log_misc, "'%s%s=%s' missing option name", subproj ? subproj : "", subproj ? ":" : "", rhs);
		return false;
	} else if (subproj && !*subproj) {
		LOG_W(log_misc, "':%s=%s' there is a colon in the option name, but no subproject was specified", lhs, rhs);
		return false;
	}

	struct option_override oo = {
		.proj = subproj,
		.name = lhs,
		.val = rhs,
	};
	darr_push(&wk->option_overrides, &oo);

	/* L(log_misc, "'%s':'%s'='%s'", subproj, lhs, rhs); */
	return true;
}

bool
opts_parse_setup(struct workspace *wk, struct setup_opts *opts,
	uint32_t argc, uint32_t argi, char *const argv[])
{
	signed char opt;

	assert(argc >= argi);

	while ((opt = getopt(argc - argi, &argv[argi],  "D:")) != -1) {
		switch (opt) {
		case 'D':
			if (!parse_config_opt(wk, optarg)) {
				return false;
			}
			break;
		default:
			LOG_W(log_misc, "unknown flag: '%c'", opt);
			return false;
		}
	}

	assert(optind >= 0);
	optind += argi;

	if ((uint32_t)optind >= argc) {
		LOG_W(log_misc, "missing build directory");
		return false;
	}

	opts->build = argv[optind];

	return true;
}

bool
opts_parse_exe(struct exe_opts *opts, uint32_t argc, uint32_t argi, char *const argv[])
{
	signed char opt;

	assert(argc >= argi);

	while ((opt = getopt(argc - argi, &argv[argi],  "c:")) != -1) {
		switch (opt) {
		case 'c':
			opts->capture = optarg;
			break;
		default:
			LOG_W(log_misc, "unknown flag: '%c'", opt);
			return false;
		}
	}

	assert(optind >= 0);
	optind += argi;

	if ((uint32_t)optind >= argc) {
		LOG_W(log_misc, "missing command");
		return false;
	}

	opts->cmd = &argv[optind];

	return true;
}

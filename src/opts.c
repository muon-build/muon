#include "posix.h"

#include <getopt.h>
#include <stdbool.h>
#include <string.h>

#include "opts.h"
#include "log.h"

bool
opts_parse_setup(struct workspace *wk, struct setup_opts *opts,
	int argc, char *const argv[])
{
	signed char opt;

	while ((opt = getopt(argc, argv,  "D:")) != -1) {
		switch (opt) {
		case 'D':
			break;
		default:
			LOG_W(log_misc, "unknown flag: '%c'", opt);
			return false;
		}
	}

	if (optind >= argc) {
		LOG_W(log_misc, "missing build directory");
		return false;
	}

	opts->build = argv[optind];

	return true;
}

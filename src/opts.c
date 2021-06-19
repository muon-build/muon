#include "posix.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "opts.h"
#include "workspace.h"

void
print_usage(FILE *f, const struct command *commands,
	const char *pre, const char *opts, const char *post)
{
	uint32_t i;
	fprintf(f, "usage: %s%s%s%s\n",
		pre,
		opts ?  " [opts]" : "",
		commands ?  " [command]" : "",
		post ? post : ""
		);

	if (opts) {
		fprintf(f,
			"opts:\n"
			"%s"
			"  -h - show this message\n",
			opts);
	}

	if (commands) {
		fprintf(f, "commands:\n");

		for (i = 0; commands[i].name; ++i) {
			fprintf(f, "  %-10s", commands[i].name);

			if (commands[i].desc) {
				fprintf(f, "- %s", commands[i].desc);
			}

			fputc('\n', f);
		}
	}
}

bool
find_cmd(const struct command *commands, cmd_func *ret,
	uint32_t argc, uint32_t argi, char *const argv[], bool optional)
{
	uint32_t i;
	const char *cmd;

	if (argi >= argc) {
		if (optional) {
			*ret = NULL;
			return true;
		} else {
			LOG_W(log_misc, "missing command");
			return false;
		}
	} else {
		cmd = argv[argi];
	}

	for (i = 0; commands[i].name; ++i) {
		if (strcmp(commands[i].name, cmd) == 0) {
			*ret = commands[i].cmd;
			return true;
		}
	}

	LOG_W(log_misc, "invalid command '%s'", cmd);
	return false;
}

bool
parse_config_key_value(struct workspace *wk, char *lhs, const char *val)
{
	char *subproj;

	subproj = lhs;
	if ((lhs = strchr(lhs, ':'))) {
		*lhs = 0;
		++lhs;
	} else {
		lhs = subproj;
		subproj = NULL;
	}

	if (!*lhs) {
		LOG_W(log_misc, "'%s%s=%s' missing option name",
			subproj ? subproj : "", subproj ? ":" : "", val);
		return false;
	} else if (subproj && !*subproj) {
		LOG_W(log_misc, "':%s=%s' there is a colon in the option name,"
			"but no subproject was specified", lhs, val);
		return false;
	}

	struct option_override oo = {
		.proj = subproj,
		.name = lhs,
		.val = val,
	};

	darr_push(&wk->option_overrides, &oo);

	return true;
}

bool
parse_config_opt(struct workspace *wk, char *lhs)
{
	char *rhs = strchr(lhs, '=');
	if (!rhs) {
		LOG_W(log_misc, "expected '=' in config opt '%s'", lhs);
		return false;
	}
	*rhs = 0;
	++rhs;

	return parse_config_key_value(wk, lhs, rhs);
}

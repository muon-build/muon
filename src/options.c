#include "options.h"
#include "log.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
static bool
parse_bool(const char *value)
{
	if (strcmp(value, "true")) {
		return true;
	} else if (strcmp(value, "false")) {
		return false;
	}

	fatal("'%s' is not a boolean value", value);
	return true;
}
*/
static bool
parse_buildtype(struct options *options, const char *value)
{
	if (strcmp(value, "plain")) {
		options->core.buildtype = BUILDTYPE_PLAIN;
	} else if (strcmp(value, "debug")) {
		options->core.buildtype = BUILDTYPE_DEBUG;
	} else if (strcmp(value, "debugoptimized")) {
		options->core.buildtype = BUILDTYPE_DEBUGOPTIMIZED;
	} else if (strcmp(value, "release")) {
		options->core.buildtype = BUILDTYPE_RELEASE;
	} else if (strcmp(value, "minsize")) {
		options->core.buildtype = BUILDTYPE_MINSIZE;
	} else if (strcmp(value, "custom")) {
		options->core.buildtype = BUILDTYPE_CUSTOM;
	} else {
		fatal("invalid build type : '%s'", value);
		return false;
	}

	return true;
}

static bool
parse_dir(struct options *options, const char *key, const char *value)
{
	return false;
}

static bool
parse_core(struct options *options, const char *key, const char *value)
{
	if (strcmp("buildtype", key) == 0) {
		return parse_buildtype(options, value);
	} else if (strcmp("warning_level", key) == 0) {
		options->core.warning_level = atoi(value);
	} else if (strcmp("werror", key) == 0) {
		if (strcmp("true", value)) {
			options->core.werror = true;
		} else if (strcmp("false", value)) {
			options->core.werror = false;
		}
	} else {
		return false;
	}

	return true;
}

static bool
parse_base(struct options *options, const char *key, const char *value)
{
	return false;
}

static bool
parse_c_std(struct options *options, const char *value)
{
	if (strcmp("c89", value) == 0) {
		options->compiler.c_std = STD_C89;
	} else if (strcmp("c99", value) == 0) {
		options->compiler.c_std = STD_C99;
	} else if (strcmp("c11", value) == 0) {
		options->compiler.c_std = STD_C11;
	} else if (strcmp("c17", value) == 0) {
		options->compiler.c_std = STD_C17;
	} else if (strcmp("c18", value) == 0) {
		options->compiler.c_std = STD_C18;
	} else if (strcmp("c2x", value) == 0) {
		options->compiler.c_std = STD_C2X;
	}  else {
		return false;
	}

	return true;
}

static bool
parse_compiler(struct options *options, const char *key, const char *value)
{
	if (strcmp("c_std", key) == 0) {
		return parse_c_std(options, value);
	}

	return false;
}

bool
options_parse(struct options *options, const char *key, const char *value)
{
	assert(options);

	return parse_dir(options, key, value)
		|| parse_core(options, key, value)
		|| parse_base(options, key, value)
		|| parse_compiler(options, key, value);
}

struct options *
options_create(void)
{
	struct options *options = calloc(1, sizeof(struct options));

	options->dir.prefix = "/usr/local";
	options->dir.bindir = "/bin";
	options->dir.datadir = "/share";
	options->dir.includedir = "/include";
	options->dir.infodir = "/share/info";
	options->dir.libdir = "/lib";
	options->dir.libexecdir = "/libexec";
	options->dir.localedir = "/share/locale";
	options->dir.localstatedir = "/var";
	options->dir.mandir = "/share/man";
	options->dir.sbindir = "/sbin";
	options->dir.sharedstatedir = "/com";
	options->dir.sysconfdir = "/etc";

	options->core.buildtype = BUILDTYPE_DEBUG;
	options->core.warning_level = 1;
	options->core.werror = false;

	options->compiler.c_args = NULL;
	options->compiler.c_link_args = NULL;
	options->compiler.c_std = STD_NONE;

	return options;
}

void
options_destroy(struct options *options)
{
	assert(options);
	free(options);
}

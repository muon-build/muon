#ifndef BOSON_OPTIONS_H
#define BOSON_OPTIONS_H

#include <stdbool.h>

enum buildtype {
	BUILDTYPE_PLAIN = 1 << 0,
	BUILDTYPE_DEBUG = 1 << 1,
	BUILDTYPE_DEBUGOPTIMIZED = 1 << 2,
	BUILDTYPE_RELEASE = 1 << 3,
	BUILDTYPE_MINSIZE = 1 << 4,
	BUILDTYPE_CUSTOM = 1 << 5,
};

enum c_std {
	STD_NONE = 1 << 0,
	STD_C89 = 1 << 1,
	STD_C99 = 1 << 2,
	STD_C11 = 1 << 3,
	STD_C17 = 1 << 4,
	STD_C18 = 1 << 5,
	STD_C2X = 1 << 6,
	STD_GNU89 = 1 << 7,
	STD_GNU99 = 1 << 8,
	STD_GNU11 = 1 << 9,
	STD_GNU17 = 1 << 10,
	STD_GNU18 = 1 << 11,
	STD_GNU2X = 1 << 12,
};

struct options {
	struct {
		char *prefix;
		char *bindir;
		char *datadir;
		char *includedir;
		char *infodir;
		char *libdir;
		char *libexecdir;
		char *localedir;
		char *localstatedir;
		char *mandir;
		char *sbindir;
		char *sharedstatedir;
		char *sysconfdir;
	} dir;

	struct {
		enum buildtype buildtype;
		int warning_level;
		bool werror;
	} core;

	struct {
		void *todo;
	} base;

	struct {
		char *c_args;
		char *c_link_args;
		enum c_std c_std;
	} compiler;
};

bool options_parse(struct options *, const char *, const char *);

struct options *options_create(void);
void options_destroy(struct options *);

#endif // BOSON_OPTIONS_H

#ifndef BOSON_VERSION_H
#define BOSON_VERSION_H
struct boson_version {
	const char *const version, *const vcs_tag;
};
extern const struct boson_version boson_version;
#endif

#include "posix.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "platform/path.h"

static void
test_path_is_absolute(void)
{
	assert(!path_is_absolute("not/absolute"));
	assert(path_is_absolute("/absolute/path"));
}

static void
test_path_join(void)
{
	char buf[PATH_MAX];

	assert(path_join(buf, PATH_MAX, "a/b", "c/d"));
	assert(strcmp(buf, "a/b/c/d") == 0);
}

static void
test_path_make_absolute(void)
{
	char cwd[PATH_MAX], buf[PATH_MAX], buf2[PATH_MAX * 2];
	assert(getcwd(cwd, PATH_MAX));

	const char *rel_path = "rel/path";

	assert(path_make_absolute(buf, PATH_MAX, rel_path));
	snprintf(buf2, PATH_MAX * 2, "%s/%s", cwd, rel_path);

	assert(strcmp(buf, buf2) == 0);
}

static void
test_path_relative_to(void)
{
	const char *tests[][3] = {
		{ "/path/to/build/",
		  "/path/to/build/tgt/dir/libfoo.a",
		  "tgt/dir/libfoo.a" },
		{ "/path/to/build",
		  "/path/to/build/libfoo.a",
		  "libfoo.a" },
		{ "/path/to/build",
		  "/path/to/src/asd.c",
		  "../src/asd.c" },
		{ "/path/to/build",
		  "/path/to/build/include",
		  "include" },
		{ "/path/to/build",
		  "/path/to/build",
		  "." },
		{ "/path/to/build/",
		  "/path/to/build",
		  "." },
		{ "/path/to/build",
		  "/path/to/build/",
		  "." },
		{ 0 },
	};
	uint32_t i;

	for (i = 0; tests[i][0]; ++i) {
		char buf[PATH_MAX];
		assert(path_relative_to(buf, PATH_MAX, tests[i][0], tests[i][1]));
		if (strcmp(buf, tests[i][2]) != 0) {
			LOG_E("'%s' != %s", buf, tests[i][2]);
			assert(false);
		}
	}
}

static void
test_path_is_basename(void)
{
	assert(!path_is_basename("a/b/c"));
	assert(path_is_basename("basename"));
}

static void
test_path_without_ext(void)
{
	const char *path = "a/b/file.txt", *no_ext = "a/b/file";
	char buf[PATH_MAX];

	assert(path_without_ext(buf, PATH_MAX, path));

	assert(strcmp(no_ext, buf) == 0);
}

static void
test_path_basename(void)
{
	const char *path = "a/b/file.txt", *basename = "file.txt";
	char buf[PATH_MAX];

	assert(path_basename(buf, PATH_MAX, path));

	assert(strcmp(basename, buf) == 0);
}

static void
test_path_dirname(void)
{
	const char *path = "a/b/file.txt", *dirname = "a/b";
	char buf[PATH_MAX];

	assert(path_dirname(buf, PATH_MAX, path));

	assert(strcmp(dirname, buf) == 0);
}

static void
test_path_is_subpath(void)
{
	assert(path_is_subpath("/a/b/c/d", "/a/b/c/d/e"));
	assert(!path_is_subpath("/a/b/c/d", "/f/b/c/d/e"));
}

static void
test_path_add_suffix(void)
{
	const char *path = "a/b/file", *w_suff = "a/b/file.txt";
	char buf[PATH_MAX];

	strcpy(buf, path);

	assert(path_add_suffix(buf, PATH_MAX, ".txt"));
	assert(strcmp(w_suff, buf) == 0);
}

static void
test_path_executable(void)
{
	const char *abs_path = "/abs/path", *rel_path = "a/b", *no_path = "file";
	char buf[PATH_MAX];

	assert(path_executable(buf, PATH_MAX, abs_path));
	assert(strcmp(buf, abs_path) == 0);

	assert(path_executable(buf, PATH_MAX, rel_path));
	assert(strcmp(buf, rel_path) == 0);

	assert(path_executable(buf, PATH_MAX, no_path));
	assert(strcmp(buf, "./file") == 0);
}

int
main(void)
{
	log_init();
	log_set_lvl(log_debug);
	assert(path_init());

	test_path_is_absolute();
	test_path_join();
	test_path_make_absolute();
	test_path_relative_to();
	test_path_is_basename();
	test_path_without_ext();
	test_path_basename();
	test_path_dirname();
	test_path_is_subpath();
	test_path_add_suffix();
	test_path_executable();
}

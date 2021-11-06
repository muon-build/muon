#include "posix.h"

#include <limits.h>

#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static bool
embed(const char *path)
{
	struct source src = { 0 };
	if (!fs_read_entire_file(path, &src)) {
		return false;
	}

	char name[PATH_MAX];
	if (!path_basename(name, PATH_MAX, path)) {
		return false;
	}
	;
	printf("{ .name = \"%s\", .src = ", name);

	uint32_t i = 0;
	for (i = 0; i < src.len; ++i) {
		if (i == 0) {
			putc('"', stdout);
		}

		if (src.src[i] == '\n') {
			printf("\\n\"\n");
			if (i != src.len - 1) {
				putc('"', stdout);
			}
		} else {
			putc(src.src[i], stdout);
		}
	}
	printf("},\n");

	fs_source_destroy(&src);
	return true;
}

int
main(int argc, char *const argv[])
{
	log_init();

	assert(argc >= 1);

	printf("uint32_t embedded_len = %d;\n"
		"\n"
		"static struct embedded_file embedded[] = {\n"
		,
		argc - 1);

	uint32_t i;
	for (i = 1; i < (uint32_t)argc; ++i) {
		if (!embed(argv[i])) {
			return 1;
		}
	}

	printf("};\n");
	return 0;
}

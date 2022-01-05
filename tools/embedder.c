#include "posix.h"

#include "buf_size.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

static void
embed_char_array(struct source *src)
{
	uint32_t i = 0;
	fputs("(char []){\n", stdout);

	for (i = 0; i < src->len; ++i) {
		printf("0x%x, ", src->src[i]);

		if ((i % 14) == 0) {
			fputs("\n", stdout);
		}
	}

	fputs("0x0\n}", stdout);
}

static void
embed_string(struct source *src)
{
	uint32_t i = 0;
	for (i = 0; i < src->len; ++i) {
		if (i == 0) {
			putc('"', stdout);
		}

		if (src->src[i] == '\n') {
			printf("\\n\"\n");
			if (i != src->len - 1) {
				putc('"', stdout);
			}
		} else {
			putc(src->src[i], stdout);
		}
	}
}

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

	if (src.len < 4096) {
		embed_string(&src);
	} else {
		embed_char_array(&src);
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

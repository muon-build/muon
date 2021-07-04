#include "posix.h"

#include <limits.h>
#include <samu.h>
#include <unistd.h>

#include "path.h"
#include "external/samu.h"
#include "filesystem.h"

const bool have_samu = true;

bool
muon_samu(uint32_t argc, char *const argv[])
{
	return samu_main(argc, (char **)argv) == 0;
}

bool
muon_samu_compdb(const char *build, const char *compile_commands)
{
	int old_stdout;
	bool ret = false;

	char cwd[PATH_MAX];
	if (!path_cwd(cwd, PATH_MAX)) {
		return false;
	}

	if (!fs_redirect(compile_commands, "w", STDOUT_FILENO, &old_stdout)) {
		return false;
	}

	if (!path_chdir(build)) {
		goto ret;
	} else if (!muon_samu(4, (char *[]){ "samu", "-t", "compdb", "c_COMPILER", NULL })) {
		goto ret;
	} else if (!path_chdir(cwd)) {
		goto ret;
	}

	ret = true;
ret:
	if (!fs_redirect_restore(STDOUT_FILENO, old_stdout)) {
		return false;
	}

	return ret;
}

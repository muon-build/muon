#include "log.h"
#include "platform/filesystem.h"

#define NOT_IMPLEMENTED do { \
		LOG_E("%s not implemented", __func__); \
		return 0; \
} while (0)

bool
fs_mkdir(const char *path)
{
	NOT_IMPLEMENTED;
}

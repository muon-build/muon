#include "posix.h"

#include "external/curl.h"
#include "log.h"

void
muon_curl_init(void)
{
}

void
muon_curl_deinit(void)
{
}

bool
muon_curl_fetch(const char *url, uint8_t **buf, uint64_t *len)
{
	LOG_W(log_misc, "curl not enabled");
	return false;
}

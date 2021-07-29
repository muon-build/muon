#include "posix.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <curl/curl.h>

#include "external/curl.h"
#include "log.h"
#include "mem.h"

const bool have_curl = true;

static struct {
	bool init;
} fetch_ctx = { 0 };

void
muon_curl_init(void)
{
	if (curl_global_init(CURL_GLOBAL_DEFAULT) == 0) {
		fetch_ctx.init = true;
	}
}

void
muon_curl_deinit(void)
{
	if (!fetch_ctx.init) {
		LOG_E("curl is not initialized");
		return;
	}

	curl_global_cleanup();
	fetch_ctx.init = false;
}

struct write_data_ctx {
	uint8_t *buf;
	uint64_t len, cap;
};

static size_t
write_data(void *src, size_t size, size_t nmemb, void *_ctx)
{
	struct write_data_ctx *ctx = _ctx;
	uint64_t want_to_write = size * nmemb;

	if (want_to_write + ctx->len > ctx->cap) {
		ctx->cap = want_to_write + ctx->len;
		ctx->buf = z_realloc(ctx->buf, ctx->cap);
	}

	memcpy(&ctx->buf[ctx->len], src, want_to_write);
	ctx->len += want_to_write;

	return nmemb;
}

bool
muon_curl_fetch(const char *url, uint8_t **buf, uint64_t *len)
{
	CURL *curl_handle;
	CURLcode err;
	char errbuf[CURL_ERROR_SIZE] = { 0 };

	LOG_I("fetching '%s", url);

	if (!fetch_ctx.init) {
		LOG_E("curl is not initialized");
		goto err0;
	}

	if (!(curl_handle = curl_easy_init())) {
		LOG_E("failed to get curl handle");
		goto err0;
	}

	if ((err = curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, errbuf)) != CURLE_OK) {
		goto err1;
	}

	/* set URL to get here */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_URL, url)) != CURLE_OK) {
		goto err1;
	}

	/* set URL to get here */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_URL, url)) != CURLE_OK) {
		goto err1;
	}

	/* Switch on full protocol/debug output while testing */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0L)) != CURLE_OK) {
		goto err1;
	}

	/* disable progress meter, set to 0L to enable it */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L)) != CURLE_OK) {
		goto err1;
	}

	/* send all data to this function  */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data)) != CURLE_OK) {
		goto err1;
	}

	struct write_data_ctx ctx = { 0 };
	/* write the page body to this file handle */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &ctx)) != CURLE_OK) {
		goto err1;
	}

	/* get it! */
	if ((err = curl_easy_perform(curl_handle)) != CURLE_OK) {
		goto err1;
	}

	*buf = ctx.buf;
	*len = ctx.len;

	/* cleanup curl stuff */
	curl_easy_cleanup(curl_handle);
	return true;

err1:
	if (*errbuf) {
		LOG_E("curl failed to fetch '%s': %s", url, errbuf);
	} else if (err != CURLE_OK) {
		LOG_E("curl failed to fetch '%s': %s", url, curl_easy_strerror(err));
	} else {
		LOG_E("curl failed to fetch '%s'", url);
	}
	curl_easy_cleanup(curl_handle);
err0:
	return false;
}

#include "posix.h"

#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "fetch.h"
#include "log.h"

static struct {
	bool init;
} fetch_ctx = { 0 };

void
fetch_init(void)
{
#ifdef BOSON_HAVE_CURL
	if (curl_global_init(CURL_GLOBAL_DEFAULT) == 0) {
		fetch_ctx.init = true;
	}
#endif
}

void
fetch_deinit(void)
{
#ifdef BOSON_HAVE_CURL
	if (!fetch_ctx.init) {
		LOG_W(log_fetch, "curl is not initialized");
		return;
	}

	curl_global_cleanup();
	fetch_ctx.init = false;
#endif
}

static size_t
write_data(void *src, size_t size, size_t nmemb, void *stream)
{
	return fwrite(src, size, nmemb, stream);
}

bool
_fetch(const char *url, const char *out_path)
{
	CURL *curl_handle;
	CURLcode err;
	FILE *out;
	char errbuf[CURL_ERROR_SIZE] = { 0 };

	LOG_I(log_fetch, "downloading '%s' to '%s'", url, out_path);

	if (!fetch_ctx.init) {
		LOG_W(log_fetch, "curl is not initialized");
		goto err0;
	}

	if (!(curl_handle = curl_easy_init())) {
		LOG_W(log_fetch, "failed to get curl handle");
		goto err0;
	}

	if ((err = curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, errbuf)) != CURLE_OK) {
		goto err2;
	}

	/* set URL to get here */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_URL, url)) != CURLE_OK) {
		goto err2;
	}

	/* set URL to get here */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_URL, url)) != CURLE_OK) {
		goto err2;
	}

	/* Switch on full protocol/debug output while testing */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0L)) != CURLE_OK) {
		goto err2;
	}

	/* disable progress meter, set to 0L to enable it */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L)) != CURLE_OK) {
		goto err2;
	}

	/* send all data to this function  */
	if ((err = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data)) != CURLE_OK) {
		goto err2;
	}

	/* open the file */
	if ((out = fopen(out_path, "wb"))) {
		/* write the page body to this file handle */
		if ((err = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, out)) != CURLE_OK) {
			goto err3;
		}

		/* get it! */
		if ((err = curl_easy_perform(curl_handle)) != CURLE_OK) {
			goto err3;
		}

		/* close the header file */
		if (fclose(out) != 0) {
			LOG_W(log_fetch, "failed to close output file '%s': %s", out_path, strerror(errno));
			goto err1;
		}
	} else {
		LOG_W(log_fetch, "failed to open output file '%s': %s", out_path, strerror(errno));
		goto err1;
	};

	/* cleanup curl stuff */
	curl_easy_cleanup(curl_handle);
	return true;

err3:
	if (fclose(out) != 0) {
		LOG_W(log_fetch, "failed to close output file '%s': %s", out_path, strerror(errno));
	}
err2:
	if (*errbuf) {
		LOG_W(log_fetch, "curl failed to fetch '%s': %s", url, errbuf);
	} else if (err != CURLE_OK) {
		LOG_W(log_fetch, "curl failed to fetch '%s': %s", url, curl_easy_strerror(err));
	} else {
		LOG_W(log_fetch, "curl failed to fetch '%s'", url);
	}
err1:
	curl_easy_cleanup(curl_handle);
err0:
	return false;
}

bool
fetch_fetch(const char *url, const char *out_path)
{
#ifdef BOSON_HAVE_CURL
	return _fetch(url, out_path);
#else
	LOG_W(log_fetch, "curl not enabled");
	return false;
#endif
}

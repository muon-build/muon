/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <curl/curl.h>
#include <stdbool.h>
#include <string.h>

#include "datastructures/bucket_arr.h"
#include "external/libcurl.h"
#include "log.h"
#include "platform/assert.h"
#include "platform/mem.h"

const bool have_libcurl = true;

static struct mc_ctx {
	CURLM *cm;
	struct bucket_arr transfers;
	bool init;
} _mc_ctx = { 0 };

struct mc_transfer_ctx {
	char errbuf[CURL_ERROR_SIZE];
	const char *url;
	CURL *handle;

	uint8_t *buf;
	uint64_t len, cap;
	uint8_t **buf_dest;
	uint64_t *len_dest;

	int32_t i;
	CURLcode err;
	bool running;
};

void
mc_init(void)
{
	struct mc_ctx *mc_ctx = &_mc_ctx;

	assert(!mc_ctx->init);

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		return;
	}

	if (!(mc_ctx->cm = curl_multi_init())) {
		return;
	}

	bucket_arr_init(&mc_ctx->transfers, 4, sizeof(struct mc_transfer_ctx));

	mc_ctx->init = true;
}

void
mc_deinit(void)
{
	struct mc_ctx *mc_ctx = &_mc_ctx;

	if (!mc_ctx->init) {
		return;
	}

	uint32_t i;
	for (i = 0; i < mc_ctx->transfers.len; ++i) {
		struct mc_transfer_ctx *ctx = bucket_arr_get(&mc_ctx->transfers, i);
		if (ctx->running) {
			LOG_E("deinit called but transfer %s is still running", ctx->url);
		}
	}

	bucket_arr_destroy(&mc_ctx->transfers);

	CURLMcode err;
	if ((err = curl_multi_cleanup(mc_ctx->cm)) != CURLM_OK) {
		LOG_E("curl: failed to cleanup: %s", curl_multi_strerror(err));
	}

	curl_global_cleanup();

	mc_ctx->init = false;
}

static size_t
mc_write_data(void *src, size_t size, size_t nmemb, void *_ctx)
{
	struct mc_transfer_ctx *ctx = _ctx;
	uint64_t want_to_write = size * nmemb;
	uint64_t newlen = want_to_write + ctx->len;

	if (newlen > ctx->cap) {
		curl_off_t content_length = 0;
		if (curl_easy_getinfo(ctx->handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length) == CURLE_OK
			&& content_length > 0) {
			ctx->cap = content_length;
		} else if (!ctx->cap) {
			ctx->cap = 1024;
		}

		while (newlen > ctx->cap) {
			ctx->cap *= 2;
		}

		ctx->buf = z_realloc(ctx->buf, ctx->cap);
	}

	memcpy(&ctx->buf[ctx->len], src, want_to_write);
	ctx->len += want_to_write;

	return nmemb;
}

static bool
mc_err(struct mc_transfer_ctx *ctx, CURLcode err, CURLMcode merr)
{
	if (*ctx->errbuf) {
		LOG_E("curl: failed to fetch '%s': %s", ctx->url, ctx->errbuf);
	} else if (err != CURLE_OK) {
		LOG_E("curl: failed to fetch '%s': %s", ctx->url, curl_easy_strerror(err));
	} else if (merr != CURLM_OK) {
		LOG_E("curl: failed to fetch '%s': %s", ctx->url, curl_multi_strerror(merr));
	} else {
		LOG_E("curl: failed to fetch '%s'", ctx->url);
	}

	curl_easy_cleanup(ctx->handle);

	return false;
}

#define mc_easy_setopt(__ctx, __y, __z)                                              \
	{                                                                            \
		CURLcode __err;                                                      \
		if ((__err = curl_easy_setopt(__ctx->handle, __y, __z)) != CURLE_OK) \
			return mc_err(__ctx, __err, CURLM_OK);                       \
	}

static bool
mc_add_transfer(struct mc_transfer_ctx *ctx)
{
	struct mc_ctx *mc_ctx = &_mc_ctx;

	if (!(ctx->handle = curl_easy_init())) {
		LOG_E("curl: failed to get curl handle");
		return false;
	}

	mc_easy_setopt(ctx, CURLOPT_ERRORBUFFER, ctx->errbuf);
	mc_easy_setopt(ctx, CURLOPT_FOLLOWLOCATION, 1);
	mc_easy_setopt(ctx, CURLOPT_URL, ctx->url);
	mc_easy_setopt(ctx, CURLOPT_VERBOSE, 0L);
	mc_easy_setopt(ctx, CURLOPT_NOPROGRESS, 1L);
	mc_easy_setopt(ctx, CURLOPT_WRITEFUNCTION, mc_write_data);
	mc_easy_setopt(ctx, CURLOPT_WRITEDATA, ctx);
	mc_easy_setopt(ctx, CURLOPT_PRIVATE, ctx);
	mc_easy_setopt(ctx, CURLOPT_BUFFERSIZE, 1*1024*1024);

	CURLMcode err;
	if ((err = curl_multi_add_handle(mc_ctx->cm, ctx->handle)) != CURLM_OK) {
		return mc_err(ctx, CURLE_OK, err);
	}

	return true;
}

int32_t
mc_fetch_begin(const char *url, uint8_t **buf, uint64_t *len, enum mc_fetch_flag flags)
{
	struct mc_ctx *mc_ctx = &_mc_ctx;
	struct mc_transfer_ctx *ctx;

	log_print(true, flags & mc_fetch_flag_verbose ? log_info : log_debug, "curl: fetching '%s'", url);

	uint32_t i;
	for (i = 0; i < mc_ctx->transfers.len; ++i) {
		ctx = bucket_arr_get(&mc_ctx->transfers, i);
		if (!ctx->running) {
			break;
		}
	}

	if (i == mc_ctx->transfers.len) {
		bucket_arr_push(&mc_ctx->transfers, &(struct mc_transfer_ctx){ 0 });
	}

	ctx = bucket_arr_get(&mc_ctx->transfers, i);
	*ctx = (struct mc_transfer_ctx){
		.running = true,
		.url = url,
		.buf_dest = buf,
		.len_dest = len,
		.i = i,
	};

	if (!mc_add_transfer(ctx)) {
		return -1;
	}

	return i;
}

enum mc_fetch_collect_result
mc_fetch_collect(int32_t i, struct mc_fetch_stats *stats)
{
	struct mc_ctx *mc_ctx = &_mc_ctx;
	struct mc_transfer_ctx *ctx;

	int _still_alive = 1;
	curl_multi_perform(mc_ctx->cm, &_still_alive);

	CURLMsg *msg;
	int msgs_left = -1;
	while ((msg = curl_multi_info_read(mc_ctx->cm, &msgs_left))) {
		if (msg->msg == CURLMSG_DONE) {
			CURL *e = msg->easy_handle;
			curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &ctx);
			ctx->err = msg->data.result;
			ctx->running = false;
			*ctx->buf_dest = ctx->buf;
			*ctx->len_dest = ctx->len;
			curl_multi_remove_handle(mc_ctx->cm, e);
			curl_easy_cleanup(e);
		} else {
			LOG_E("curl: failed to read message (CURLMsg:%d)", msg->msg);
			return mc_fetch_collect_result_error;
		}
	}

	ctx = bucket_arr_get(&mc_ctx->transfers, i);

	curl_off_t content_length = 0;
	if (curl_easy_getinfo(ctx->handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length) != CURLE_OK) {
		content_length = 0;
	}
	stats->total = content_length;
	stats->downloaded = ctx->len;

	if (!ctx->running) {
		if (ctx->err != CURLE_OK) {
			mc_err(ctx, ctx->err, CURLM_OK);
			return mc_fetch_collect_result_error;
		}

		return mc_fetch_collect_result_done;
	}

	return mc_fetch_collect_result_pending;
}

bool
mc_wait(uint32_t timeout_ms)
{
	struct mc_ctx *mc_ctx = &_mc_ctx;
	CURLMcode err;
	if ((err = curl_multi_wait(mc_ctx->cm, NULL, 0, timeout_ms, NULL)) != CURLM_OK) {
		LOG_E("curl: failed to wait: %s", curl_multi_strerror(err));
		return false;
	}

	return true;
}

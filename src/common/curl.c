/*
 * NVMe over Fabrics Distributed Endpoint Management (NVMe-oF DEM).
 * Copyright (c) 2017-2018 Intel Corporation, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <curl/curl.h>

#include "curl.h"

struct curl_context {
	CURL *curl;
	char *write_data;	/* used in write_cb */
	size_t write_sz;
	char *read_data;	/* used in read_cb */
	int read_sz;
};

static struct curl_context	*ctx;
static int			 debug_curl;

#ifdef CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
#define CURLINFO_CONTENT_LENGTH CURLINFO_CONTENT_LENGTH_DOWNLOAD_T
#else
#define CURLINFO_CONTENT_LENGTH CURLINFO_CONTENT_LENGTH_DOWNLOAD
#endif

static size_t read_cb(char *p, size_t size, size_t n, void *stream)
{
	int			 len = size * n;
	int			 cnt;

	if (ctx != stream || !ctx->read_sz)
		cnt = 0;
	else if (len > ctx->read_sz) {
		memcpy(p, ctx->read_data, ctx->read_sz);
		cnt = ctx->read_sz;
		ctx->read_sz = 0;
	} else {
		memcpy(p, ctx->read_data, len);
		ctx->read_data += len;
		ctx->read_sz -= len;
		cnt = len;
	}

	return cnt;
}

static size_t write_cb(void *contents, size_t size, size_t n, void *stream)
{
	size_t			 bytes = size * n;

	if (ctx != stream)
		return 0;

	ctx->write_data = realloc(ctx->write_data, ctx->write_sz + bytes + 1);
	if (ctx->write_data == NULL) {
		fprintf(stderr, "unable to alloc memory for new data\n");
		return 0;
	}

	memcpy(&(ctx->write_data[ctx->write_sz]), contents, bytes);
	ctx->write_sz += bytes;
	ctx->write_data[ctx->write_sz] = 0;

	return bytes;
}

int init_curl(int debug)
{
	CURL			*curl;

	debug_curl = debug;

	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		fprintf(stderr, "unable to alloc memory for curl context\n");
		return -ENOMEM;
	}

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	if (!curl) {
		fprintf(stderr, "unable to init curl");
		free(ctx);
		return -EINVAL;
	}

	/* will be grown as needed by the realloc in write_cb */
	ctx->write_data = malloc(1);
	ctx->write_sz = 0;    /* no data at this point */
	ctx->write_data[0] = 0;

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,	(void *) write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA,	(void *) ctx);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION,	(void *) read_cb);
	curl_easy_setopt(curl, CURLOPT_READDATA,	(void *) ctx);

	ctx->curl = curl;

	return 0;
}

void cleanup_curl(void)
{
	CURL			*curl = ctx->curl;

	curl_easy_cleanup(curl);

	curl_global_cleanup();

	free(ctx->write_data);
	free(ctx);
}

static int exec_curl(char *url, char **p)
{
	CURL			*curl = ctx->curl;
	CURLcode		 ret;
	curl_off_t		 bytes;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	ret = curl_easy_perform(curl);

	if (ret == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH, &bytes);
		*p = strndup(ctx->write_data, bytes);
	} else if (ret == CURLE_COULDNT_CONNECT) {
		fprintf(stderr, "curl returned error %s (%d) errno %d\n",
			curl_easy_strerror(ret), ret, errno);
		ret = -errno;
	} else
		ret = -EINVAL;

	if (ctx->write_sz) {
		free(ctx->write_data);
		ctx->write_data = malloc(1);
		ctx->write_sz = 0;
		ctx->write_data[0] = 0;
	}

	return ret;
}

int exec_get(char *url, char **result)
{
	CURL			*curl = ctx->curl;
	int			 ret;

	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

	if (debug_curl)
		printf("GET %s\n", url);

	ret = exec_curl(url, result);

	curl_easy_setopt(curl, CURLOPT_HTTPGET, 0);

	return ret;
}

int exec_put(char *url, char *data, int len)
{
	CURL			*curl = ctx->curl;
	char			*result;
	int			 ret;

	curl_easy_setopt(curl, CURLOPT_PUT, 1);

	ctx->read_data = data;
	ctx->read_sz = len;

	if (debug_curl) {
		printf("PUT %s\n", url);
		if (len)
			printf("<< %.*s >>\n", len, data);
	}

	ret = exec_curl(url, &result);

	curl_easy_setopt(curl, CURLOPT_PUT, 0);

	if (ret)
		return ret;

	printf("%s\n", result);

	free(result);

	return 0;
}

int exec_post(char *url, char *data, int len)
{
	CURL			*curl = ctx->curl;
	char			*result;
	int			 ret;

	curl_easy_setopt(curl, CURLOPT_HTTPPOST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);

	if (debug_curl) {
		printf("POST %s\n", url);
		if (len)
			printf("<< %.*s >>\n", len, data);
	}

	ret = exec_curl(url, &result);

	curl_easy_setopt(curl, CURLOPT_HTTPPOST, 0);

	if (ret)
		return ret;

	printf("%s\n", result);

	free(result);

	return 0;
}

int exec_delete(char *url)
{
	CURL			*curl = ctx->curl;
	char			*result;
	int			 ret;

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

	if (debug_curl)
		printf("DELETE %s\n", url);

	ret = exec_curl(url, &result);

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);

	if (ret)
		return ret;

	printf("%s\n", result);

	free(result);

	return 0;
}

int exec_delete_ex(char *url, char *data, int len)
{
	CURL			*curl = ctx->curl;
	char			*result;
	int			 ret;

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);

	if (debug_curl) {
		printf("DELETE %s\n", url);
		if (len)
			printf("<< %.*s >>\n", len, data);
	}

	ret = exec_curl(url, &result);

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);

	if (ret)
		return ret;

	printf("%s\n", result);

	free(result);

	return 0;
}

int exec_patch(char *url, char *data, int len)
{
	CURL			*curl = ctx->curl;
	char			*result;
	int			 ret;

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);

	if (debug_curl) {
		printf("PATCH %s\n", url);
		if (len)
			printf("<< %.*s >>\n", len, data);
	}

	ret = exec_curl(url, &result);

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);

	if (ret)
		return ret;

	printf("%s\n", result);

	free(result);

	return 0;
}

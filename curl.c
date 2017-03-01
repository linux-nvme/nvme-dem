/*
 * Distributed Endpoint Manager.
 * Copyright (c) 2017 Intel Corporation., Inc. All rights reserved.
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
#include <curl/curl.h>

#include "curl.h"

struct curl_context {
	CURL *curl;
	char *memory;
	size_t size;
	char *data;
	int len;
};

static size_t read_cb(char *p, size_t size, size_t n, void *stream)
{
	struct curl_context *ctx = stream;
	int len = size * n;
	int cnt;

	if (!ctx->len)
		cnt = 0;
	else if (len > ctx->len) {
		memcpy(p, ctx->data, ctx->len);
		cnt = ctx->len;
		ctx->len = 0;
	} else {
		memcpy(p, ctx->data, len);
		ctx->data += len;
		ctx->len -= len;
		cnt = len;
	}

	return cnt;
}

static size_t write_cb(void *contents, size_t size, size_t n, void *p)
{
	struct curl_context *ctx = p;
	size_t realsize = size * n;

	ctx->memory = realloc(ctx->memory, ctx->size + realsize + 1);
	if (ctx->memory == NULL) {
		fprintf(stderr, "unable to alloc memory for new data\n");
		return 0;
	}

	memcpy(&(ctx->memory[ctx->size]), contents, realsize);
	ctx->size += realsize;
	ctx->memory[ctx->size] = 0;

	return realsize;
}

void *init_curl()
{
	CURL *curl;
	CURLcode res;
	struct curl_context *ctx;

	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		fprintf(stderr, "unable to alloc memory for curl context\n");
		return NULL;
	}

	/* will be grown as needed by the realloc above */
	ctx->memory = malloc(1);
	ctx->size = 0;    /* no data at this point */
	ctx->memory[0] = 0;

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	if (!curl) {
		fprintf(stderr, "unable to init curl");
		free(ctx);
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) ctx);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_cb);
	curl_easy_setopt(curl, CURLOPT_READDATA, (void *) ctx);

	ctx->curl = curl;
	return ctx;
}

void cleanup_curl(void *p)
{
	struct curl_context *ctx = p;
	CURL *curl = ctx->curl;
	CURLcode res;

	curl_easy_cleanup(curl);

	curl_global_cleanup();
}

static int exec_curl(struct curl_context *ctx, char *url)
{
	CURL *curl = ctx->curl;
	CURLcode ret;

	curl_easy_setopt(curl, CURLOPT_URL, url);

	ret = curl_easy_perform(curl);

	printf("%s\n", url);

	if (!ret)
		printf("%s\n", ctx->memory);
	else
		fprintf(stderr, "curl returned error %s (%d)\n",
			curl_easy_strerror(ret), ret);

	if (ctx->size) {
		free(ctx->memory);
		ctx->memory = malloc(1);
		ctx->size = 0;
		ctx->memory[0] = 0;
	}

	return ret;
}

int exec_get(void *p, char *url)
{
	struct curl_context *ctx = p;
	CURL *curl = ctx->curl;
	int ret;

	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

	ret = exec_curl(ctx, url);

	curl_easy_setopt(curl, CURLOPT_HTTPGET, 0);

	return ret;
}

int exec_put(void *p, char *url, char *data, int len)
{
	struct curl_context *ctx = p;
	CURL *curl = ctx->curl;
	int ret;

	curl_easy_setopt(curl, CURLOPT_PUT, 1);

	ctx->data = data;
	ctx->len = len;

	ret = exec_curl(ctx, url);

	curl_easy_setopt(curl, CURLOPT_PUT, 0);

	return ret;
}

int exec_post(void *p, char *url, char *data, int len)
{
	struct curl_context *ctx = p;
	CURL *curl = ctx->curl;
	int ret;

	curl_easy_setopt(curl, CURLOPT_HTTPPOST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);

	ret = exec_curl(ctx, url);

	curl_easy_setopt(curl, CURLOPT_HTTPPOST, 0);

	return ret;
}

int exec_delete(void *p, char *url)
{
	struct curl_context *ctx = p;
	CURL *curl = ctx->curl;
	int ret;

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

	ret = exec_curl(ctx, url);

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);

	return ret;
}

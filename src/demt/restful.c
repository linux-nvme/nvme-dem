/*
 * NVMe over Fabrics Distributed Endpoint Manager (NVMe-oF DEM).
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

#include "mongoose.h"

#include "common.h"
#include "tags.h"

static const struct mg_str s_get_method = MG_MK_STR("GET");
static const struct mg_str s_put_method = MG_MK_STR("PUT");
static const struct mg_str s_post_method = MG_MK_STR("POST");
static const struct mg_str s_patch_method = MG_MK_STR("PATCH");
static const struct mg_str s_option_method = MG_MK_STR("OPTION");
static const struct mg_str s_delete_method = MG_MK_STR("DELETE");

#define HTTP_HDR			"HTTP/1.1"
#define SMALL_RSP			128
#define LARGE_RSP			512
#define BODY_SZ				1024

#define HTTP_OK				200
#define HTTP_ERR_NOT_FOUND		402
#define HTTP_ERR_INTERNAL		403
#define HTTP_ERR_PAGE_NOT_FOUND		404
#define HTTP_ERR_NOT_IMPLEMENTED	405
#define HTTP_ERR_CONFLICT		409

static int is_equal(const struct mg_str *s1, const struct mg_str *s2)
{
	return s1->len == s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}

static inline int http_error(int err)
{
	if (err == 0)
		return 0;

	if (err == -ENOENT)
		return HTTP_ERR_NOT_FOUND;

	if (err == -EEXIST)
		return HTTP_ERR_CONFLICT;

	return HTTP_ERR_INTERNAL;
}

static int bad_request(char *resp)
{
	strcpy(resp, "Method Not Implemented");

	return HTTP_ERR_NOT_IMPLEMENTED;
}

static int parse_uri(char *p, int depth, char *part[])
{
	int			 i = -1;

	depth++;
	for (; depth && *p; p++)
		if (*p == '/') {
			*p = '\0';
			i++, depth--;
			if (depth) part[i] = &p[1];
		}

	return i;
}

#define MAX_DEPTH 8

void handle_http_request(struct mg_connection *c, void *ev_data)
{
	struct http_message	*hm = (struct http_message *) ev_data;
	char			*resp = NULL;
	char			*uri = NULL;
	char			 error[16];
	char			*parts[MAX_DEPTH] = { NULL };
	int			 ret;
	int			 n;

	if (!hm->uri.len) {
		strcpy(resp, "Bad page no uri");
		strcpy(error, "Page Not Found");
		ret = HTTP_ERR_PAGE_NOT_FOUND;
		goto out;
	}

	resp = malloc(BODY_SZ);
	if (!resp) {
		strcpy(resp, "No memory!");
		strcpy(error, "No Memory");
		ret = HTTP_ERR_INTERNAL;
		goto out;
	}

	print_debug("%.*s %.*s", (int) hm->method.len, hm->method.p,
		    (int) hm->uri.len, hm->uri.p);

	if (hm->body.len)
		print_debug("%.*s", (int) hm->body.len, hm->body.p);

	memset(resp, 0, BODY_SZ);

	uri = malloc(hm->uri.len + 1);
	if (!uri) {
		strcpy(resp, "No memory!");
		strcpy(error, "No Memory");
		ret = HTTP_ERR_INTERNAL;
		goto out;
	}
	memcpy(uri, (char *) hm->uri.p, hm->uri.len);
	uri[hm->uri.len] = 0;

	n = parse_uri(uri, MAX_DEPTH, parts);
	if (n < 0)
		goto bad_page;

	sprintf(resp, "TODO Handle %.*s", (int) hm->uri.len, hm->uri.p);
	goto out;

bad_page:
	sprintf(resp, "Bad page %.*s", (int) hm->uri.len, hm->uri.p);
	strcpy(error, "Page Not Found");
	ret = HTTP_ERR_PAGE_NOT_FOUND;
out:
	if (!ret)
		mg_printf(c, "%s %d OK", HTTP_HDR, HTTP_OK);
	else
		mg_printf(c, "%s %d %s", HTTP_HDR, ret, resp);

	mg_printf(c, "\r\nContent-Length: %ld\r\n\r\n%s\r\n\r\n",
		  strlen(resp), resp);
	if (uri)
		free(uri);
	if (resp)
		free(resp);

	c->flags = MG_F_SEND_AND_CLOSE;
}

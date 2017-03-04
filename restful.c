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

#include "mongoose.h"

#include "common.h"
#include "json.h"
#include "tags.h"

static const struct mg_str s_get_method = MG_MK_STR("GET");
static const struct mg_str s_put_method = MG_MK_STR("PUT");
static const struct mg_str s_post_method = MG_MK_STR("POST");
static const struct mg_str s_patch_method = MG_MK_STR("PATCH");
static const struct mg_str s_option_method = MG_MK_STR("OPTION");
static const struct mg_str s_delete_method = MG_MK_STR("DELETE");

#define min(x, y) ((x < y) ? x : y)
#define SMALL_RSP 128
#define LARGE_RSP 512
#define BODY_SZ   1024

#define HTTP_ERR_NOT_FOUND 402
#define HTTP_ERR_NOT_IMPLEMENTED 405
#define HTTP_OK 200
#define HTTP_ERR_INTERNAL 403
#define HTTP_ERR_PAGE_NOT_FOUND 404

static int is_equal(const struct mg_str *s1, const struct mg_str *s2)
{
	return s1->len == s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}

int  bad_request(char *response)
{
	strcpy(response, "Method Not Implemented");

	return HTTP_ERR_NOT_IMPLEMENTED;
}

int get_dem_request(char *response)
{
	// TODO build real config info in response

	strcpy(response, "Config of DEM");

	return 0;
}

int post_dem_request(struct http_message *hm, char *response)
{
	char body[SMALL_RSP+1];
	int ret = 0;

	memset(body, 0, sizeof(body));
	strncpy(body, hm->body.p, min(SMALL_RSP, hm->body.len));

	if (strcmp(body, METHOD_SHUTDOWN) == 0) {
		shutdown_dem();
		strcpy(response, "DEM shutting down");
	} else {
		ret = HTTP_ERR_NOT_IMPLEMENTED;
		strcpy(response, "Method Not Implemented");
	}

	return ret;
}

int handle_dem_requests(struct http_message *hm, char *response)
{
	int ret;

	if (is_equal(&hm->method, &s_get_method))
		ret = get_dem_request(response);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_dem_request(hm, response);
	else
		ret = bad_request(response);

	return ret;
}

int  get_ctrl_request(void *ctx, char *ctrl, char *response)
{
	int ret;

	if (!ctrl) {
		ret = list_ctrl(ctx, response);
		if (ret) {
			strcpy(response, "No Controllers configured");
			ret = HTTP_ERR_NOT_FOUND;
		}
	} else {
		ret = show_ctrl(ctx, ctrl, response);
		if (ret) {
			sprintf(response, "Contorller %s not found", ctrl);
			ret = HTTP_ERR_NOT_FOUND;
		}
	}

	return ret;
}

int delete_ctrl_request(void *ctx, char *ctrl, char *ss, char *response)
{
	int ret = 0;

	if (ss) {
		ret = del_subsys(ctx, ctrl, ss);
		if (!ret)
			sprintf(response, "Subsystem %s deleted "
				"from Controller %s", ss, ctrl);
		else {
			sprintf(response, "Subsystem %s not found "
				"for Controller %s", ss, ctrl);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}
	} else {
		ret = del_ctrl(ctx, ctrl);
		if (!ret)
			sprintf(response, "Controller %s deleted", ctrl);
		else {
			sprintf(response, "Controller %s not found", ctrl);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}
	}

	store_config_file(ctx);
out:
	return ret;
}

int put_ctrl_request(void *ctx, char *ctrl, char *ss, struct mg_str *body,
		     char *response)
{
	char data[LARGE_RSP+1];
	int ret;

	if (ss) {
		ret = add_subsys(ctx, ctrl, ss);
		if (ret) {
			sprintf(response, "Controller %s not found", ctrl);
			goto out;
		}
	} else {
		memset(data, 0, sizeof(data));
		strncpy(data, body->p, min(LARGE_RSP, body->len));

		add_ctrl(ctx, ctrl); // if exists, this must be an update

		ret = set_ctrl(ctx, ctrl, data);
		if (ret) {
			sprintf(response, "Could not update Controller %s",
				ctrl);
			ret = HTTP_ERR_INTERNAL;
			goto out;
		}

		sprintf(response, "Controller %s updated", ctrl);
	}

	store_config_file(ctx);
out:
	return ret;
}

int  post_ctrl_request(void *ctx, char *ctrl, struct mg_str *body,
		       char *response)
{
	char new[SMALL_RSP+1];
	int ret;

	if (body->len) {
	memset(new, 0, sizeof(new));
	strncpy(new, body->p, min(LARGE_RSP, body->len));

	ret = rename_ctrl(ctx, ctrl, new);

	if (ret) {
		sprintf(response, "Controller %s not found or "
			"Controller %s already exists", ctrl, new);
		ret = HTTP_ERR_NOT_FOUND;
		goto out;
	}

	sprintf(response, "Controller %s renamed to %s", ctrl, new);

	store_config_file(ctx);
	} else {
		// TODO refresh controller
		ret = bad_request(response);
	}
out:
	return ret;
}

int handle_ctrl_requests(void *ctx, struct http_message *hm, char *response)
{
	char *url;
	char *ctrl = NULL;
	char *ss = NULL;
	char *p;
	int ret;
	int len = hm->uri.len;

	url = malloc(len + 1);
	if (!url)
		return -ENOMEM;

	strncpy(url, hm->uri.p, len);
	url[len] = 0;

	strtok_r(url, "/", &p); // checked before calling handler
	ctrl = strtok_r(NULL, "/", &p);
	if (ctrl)
		ss = strtok_r(NULL, "/", &p);

	if (is_equal(&hm->method, &s_get_method))
		ret = get_ctrl_request(ctx, ctrl, response);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_ctrl_request(ctx, ctrl, ss, &hm->body, response);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_ctrl_request(ctx, ctrl, ss, response);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_ctrl_request(ctx, ctrl, &hm->body, response);
	else
		ret = bad_request(response);

	return ret;
}

int get_host_request(void *ctx, char *host, char *response)
{
	int ret;

	if (!host) {
		ret = list_host(ctx, response);
		if (ret) {
			strcpy(response, "No Host configured");
			ret = HTTP_ERR_NOT_FOUND;
		}
	} else {
		ret = show_host(ctx, host, response);
		if (ret) {
			sprintf(response, "Host %s not found", host);
			ret = HTTP_ERR_NOT_FOUND;
		}
	}

	return ret;
}

int delete_host_request(void *ctx, char *host, char *ss, char *response)
{
	int ret;

	if (ss) {
		ret = del_acl(ctx, host, ss);
		if (!ret)
			sprintf(response, "Subsystem %s deleted "
				"from acl for host %s", ss, host);
		else {
			sprintf(response, "Subsystem %s not found "
				"in acl for host %s", ss, host);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}
	} else {
		ret = del_host(ctx, host);
		if (!ret)
			sprintf(response, "Host %s deleted", host);
		else {
			sprintf(response, "Host %s not found", host);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}
	}

	store_config_file(ctx);
out:
	return ret;
}

int put_host_request(void *ctx, char *host, char *ss, char *response)
{
	int ret;

	if (ss) {
		ret = add_acl(ctx, host, ss);
		if (!ret)
			sprintf(response, "Subsystem %s added "
				"to acl for host %s", ss, host);
		else {
			sprintf(response, "Host %s not found", host);
			goto out;
		}
	} else {
		ret = add_host(ctx, host);
		if (!ret)
			sprintf(response, "Host %s added", host);
		else {
			sprintf(response, "Host %s exists", host);
			ret = HTTP_ERR_INTERNAL;
			goto out;
		}
	}

	store_config_file(ctx);
out:
	return ret;
}

int post_host_request(void *ctx, char *host, char *ss, struct mg_str *body,
		      char *response)
{
	char new[SMALL_RSP+1];
	int ret;

	memset(new, 0, sizeof(new));
	strncpy(new, body->p, min(LARGE_RSP, body->len));

	if (ss) { // TODO may add changing ACL access R/W/RW
		ret = bad_request(response);
		goto out;
	} else {
		ret = rename_host(ctx, host, new);

		if (ret) {
			sprintf(response, "Host %s not found or "
				"Host %s already exists", host, new);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}

		sprintf(response, "Host %s renamed to %s", host, new);

	}
	store_config_file(ctx);
out:
	return ret;
}

int handle_host_requests(void *ctx, struct http_message *hm, char *response)
{
	char *url;
	char *host;
	char *ss = NULL;
	char *p;
	int ret;
	int len = hm->uri.len;

	url = malloc(len + 1);
	if (!url)
		return -ENOMEM;

	strncpy(url, hm->uri.p, len);
	url[len] = 0;

	strtok_r(url, "/", &p); // checked before calling handler
	host = strtok_r(NULL, "/", &p);
	if (host)
		ss = strtok_r(NULL, "/", &p);

	if (is_equal(&hm->method, &s_get_method))
		ret = get_host_request(ctx, host, response);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_host_request(ctx, host, ss, response);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_host_request(ctx, host, ss, response);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_host_request(ctx, host, ss, &hm->body, response);
	else
		ret = bad_request(response);

	return ret;
}

void handle_http_request(void *ctx, struct mg_connection *c, void *ev_data)
{
	struct http_message *hm = (struct http_message *) ev_data;
	char *target = (char *) &hm->uri.p[1];
	char *response;
	int ret;

	if (!hm->uri.len) {
		mg_printf(c, "HTTP/1.1 %d Page Not Found\r\n\r\n",
                          HTTP_ERR_PAGE_NOT_FOUND);
		goto out1;
	}

	response = malloc(BODY_SZ);
	if (!response) {
		fprintf(stderr, "no memory!\n");
		mg_printf(c, "HTTP/1.1 %d No Memory\r\n\r\n",
			  HTTP_ERR_INTERNAL);
		goto out1;
	}

	memset(response, 0, BODY_SZ);

	if (strncmp(target, TARGET_DEM, DEM_LEN) == 0)
		ret = handle_dem_requests(hm, response);
	else if (strncmp(target, TARGET_CTRL, CTRL_LEN) == 0)
		ret = handle_ctrl_requests(ctx, hm, response);
	else if (strncmp(target, TARGET_HOST, HOST_LEN) == 0)
		ret = handle_host_requests(ctx, hm, response);
	else {
		fprintf(stderr, "Bad page %*s\n", (int) hm->uri.len, hm->uri.p);
		mg_printf(c, "HTTP/1.1 %d Page Not Found\r\n\r\n",
			  HTTP_ERR_PAGE_NOT_FOUND);
		goto out2;
	}

	if (!ret)
		mg_printf(c, "HTTP/1.1 %d OK\r\n", HTTP_OK);
	else
		mg_printf(c, "HTTP/1.1 %d %s\r\n", ret, response);

	mg_printf(c, "Content-Length: %ld\r\n\r\n%s",
		  strlen(response), response);
out2:
	free(response);
out1:
	c->flags = MG_F_SEND_AND_CLOSE;
}

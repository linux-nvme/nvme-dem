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
#include "json.h"
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
	int			i = -1;

	depth++;
	for (; depth && *p; p++)
		if (*p == '/') {
			*p = '\0';
			i++, depth--;
			if (depth) part[i] = &p[1];
		}

	return i;
}

static int get_dem_request(char *verb, char *resp)
{
	struct interface	*iface = interfaces;
	int			 i;
	int			 n = 0;

	if (verb)
		return bad_request(resp);

	// TODO Ping Sujoy - Rackscale may need info / provide input

	n = sprintf(resp, "{\"%s\":[", TAG_INTERFACES);
	resp += n;

	for (i = 0; i < num_interfaces; i++, iface++) {
		n = sprintf(resp, "%s{\"%s\":%d,", (i) ? "," : "", TAG_ID, i);
		resp += n;

		n = sprintf(resp, "\"%s\":\"%s\",", TAG_TYPE, iface->trtype);
		resp += n;

		n = sprintf(resp, "\"%s\":\"%s\",", TAG_FAMILY,
			    iface->addrfam);
		resp += n;

		n = sprintf(resp, "\"%s\":\"%s\",", TAG_ADDRESS,
			    iface->address);
		resp += n;

		n = sprintf(resp, "\"%s\":%s}", TAG_PORT,
			    iface->pseudo_target_port);
		resp += n;
	}

	sprintf(resp, "]}");

	return 0;
}

static int post_dem_request(char *verb, struct http_message *hm, char *resp)
{
	int			 ret = 0;

	if (hm->body.len)
		ret = bad_request(resp);
	else if (strlen(METHOD_SHUTDOWN) == strlen(verb) &&
		 strcmp(verb, METHOD_SHUTDOWN) == 0) {
		shutdown_dem();
		strcpy(resp, "DEM shutting down");
	} else if (strlen(METHOD_APPLY) == strlen(verb) &&
		   strcmp(verb, METHOD_APPLY) == 0) {
		ret = restart_dem();
		if (ret < 0) {
			sprintf(resp, "DEM config apply failed %d", ret);
			ret = HTTP_ERR_INTERNAL;
		}
		strcpy(resp, "DEM config applied");
	} else {
		ret = HTTP_ERR_NOT_IMPLEMENTED;
		strcpy(resp, "Method Not Implemented");
	}

	return ret;
}

static int handle_dem_requests(char *verb, struct http_message *hm, char *resp)
{
	int			 ret;

	if (is_equal(&hm->method, &s_get_method))
		ret = get_dem_request(verb, resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_dem_request(verb, hm, resp);
	else
		ret = bad_request(resp);

	return ret;
}

static int get_ctlr_request(void *ctx, char *group, char *ctlr, char *resp)
{
	int			 ret;

	if (!ctlr || !*ctlr)
		ret = list_ctlr(ctx, group, resp);
	else
		ret = show_ctlr(ctx, group, ctlr, resp);

	return http_error(ret);
}

static int delete_ctlr_request(void *ctx, char *group, char *ctlr, char *ss,
			       char *resp)
{
	int			 ret = 0;

	if (ss && *ss) {
		ret = del_subsys(ctx, group, ctlr, ss, resp);
		if (ret)
			goto out;
	} else {
		ret = del_ctlr(ctx, group, ctlr, resp);
		if (ret)
			 goto out;
	}

	store_config_file(ctx);

	return 0;
out:
	return http_error(ret);
}

static int put_ctlr_request(void *ctx, char *group, char *ctlr, char *ss,
			    struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (ss) {
		ret = set_subsys(ctx, group, ctlr, ss, data, 1, resp);
		if (ret)
			goto out;
	} else if (ctlr) {
		ret = add_a_ctlr(ctx, group, ctlr, resp);
		if (ret)
			goto out;
	} else {
		ret = add_to_ctlrs(ctx, group, data, resp);
		if (ret)
			goto out;
	}

	store_config_file(ctx);

	return 0;
out:
	return http_error(ret);
}

static int post_ctlr_request(void *ctx, char *group, char *ctlr, char *ss,
			     struct mg_str *body, char *resp)
{
	char			 data[SMALL_RSP + 1];
	int			 ret;

	if (body->len) {
		memset(data, 0, sizeof(data));
		strncpy(data, body->p, min(LARGE_RSP, body->len));

		if (ss) {
			ret = set_subsys(ctx, group, ctlr, ss, data, 0, resp);
			if (ret)
				goto out;
		} else {
			ret = add_to_ctlrs(ctx, group, ctlr, resp);
			if (ret)
				goto out;
		}
	} else if (!ss) {
		ret = add_a_ctlr(ctx, group, ctlr, resp);
		if (ret)
			goto out;
	} else if (!strcmp(ss, METHOD_REFRESH)) {
		ret = refresh_ctlr(ctlr);
		if (ret) {
			sprintf(resp, "%s '%s' not found in %s '%s'",
				TAG_CTLR, ctlr, TAG_GROUP, group);
			ret = HTTP_ERR_INTERNAL;
			goto out;
		}

		sprintf(resp, "%s '%s' refreshed in %s '%s'",
			TAG_CTLR, ctlr, TAG_GROUP, group);

		goto out;
	} else {
		ret = set_subsys(ctx, group, ctlr, ss,
				 "{ \"AllowAllHosts\" : 0 }", 1, resp);
		if (ret)
			goto out;
	}

	store_config_file(ctx);

	return 0;
out:
	return http_error(ret);
}

static int patch_ctlr_request(void *ctx, char *group, char *ctlr, char *ss,
			      struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret;

	if (!body->len) {
		sprintf(resp, "no data provided");
		ret = -EINVAL;
		goto out;
	}

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (ss) {
		ret = set_subsys(ctx, group, ctlr, ss, data, 0, resp);
		if (ret)
			goto out;
	} else {
		ret = update_ctlr(ctx, group, ctlr, data, resp);
		if (ret)
			goto out;
	}

	store_config_file(ctx);

	return 0;
out:
	return http_error(ret);
}

static int handle_ctlr_requests(void *ctx, char *parts[],
				struct http_message *hm, char *resp)
{
	char			*group;
	char			*ctlr;
	char			*ss;
	int			 ret;

	group = parts[1];
	ctlr = parts[3];
	ss = parts[4];

	if (is_equal(&hm->method, &s_get_method))
		ret = get_ctlr_request(ctx, group, ctlr, resp);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_ctlr_request(ctx, group, ctlr, ss, &hm->body, resp);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_ctlr_request(ctx, group, ctlr, ss, resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_ctlr_request(ctx, group, ctlr, ss, &hm->body, resp);
	else if (is_equal(&hm->method, &s_patch_method))
		ret = patch_ctlr_request(ctx, group, ctlr, ss, &hm->body, resp);
	else
		ret = bad_request(resp);

	return ret;
}

static int get_host_request(void *ctx, char *group, char *host, char *resp)
{
	int			 ret;

	if (!host)
		ret = list_host(ctx, group, resp);
	else
		ret = show_host(ctx, group, host, resp);

	return http_error(ret);
}

static int delete_host_request(void *ctx, char *group, char *host, char *ss,
			       char *resp)
{
	int			 ret;

	if (ss) {
		ret = del_acl(ctx, group, host, ss, resp);
		if (ret)
			goto out;
	} else {
		ret = del_host(ctx, group, host, resp);
		if (ret)
			goto out;
	}

	store_config_file(ctx);

	return 0;
out:
	return http_error(ret);
}

static int put_host_request(void *ctx, char *group, char *host, char *ss,
			    struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (ss) {
		ret = set_acl(ctx, group, host, ss, data, resp);
		if (ret)
			goto out;
	} else if (host) {
		ret = add_a_host(ctx, group, host, resp);
		if (ret)
			goto out;
	} else {
		ret = add_to_hosts(ctx, group, data, resp);
		if (ret)
			goto out;
	}

	store_config_file(ctx);

	return 0;
out:
	return http_error(ret);
}

static int post_host_request(void *ctx, char *group, char *host,
			     struct mg_str *body, char *resp)
{
	char			 data[SMALL_RSP + 1];
	int			 ret;

	if (!body->len) {
		ret = add_a_host(ctx, group, host, resp);
		if (ret)
			return HTTP_ERR_INTERNAL;
	} else {
		memset(data, 0, sizeof(data));
		strncpy(data, body->p, min(LARGE_RSP, body->len));

		ret = update_host(ctx, group, host, data, resp);
		if (ret)
			ret = HTTP_ERR_NOT_FOUND;
	}

	store_config_file(ctx);

	return 0;
}

static int patch_host_request(void *ctx, char *group, char *host, char *ss,
			      struct mg_str *body, char *resp)
{
	char			 data[SMALL_RSP + 1];
	int			 ret;

	if (!body->len) {
		bad_request(resp);
		ret = HTTP_ERR_INTERNAL;
		goto out;
	}

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (!ss) {
		ret = update_host(ctx, group, host, data, resp);
		if (ret)
			goto out;
	} else {
		ret = set_acl(ctx, group, host, ss, data, resp);
		if (ret)
			goto out;
	}

	store_config_file(ctx);

	return 0;
out:
	return http_error(ret);
}

static int get_group_request(void *ctx, char *group, char *resp)
{
	int			 ret;

	if (!group)
		ret = list_group(ctx, resp);
	else
		ret = show_group(ctx, group, resp);

	return http_error(ret);
}
static int put_group_request(void *ctx, struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	ret = add_to_groups(ctx, data, resp);
	if (ret)
		return http_error(ret);

	store_config_file(ctx);

	return 0;
}

static int post_group_request(void *ctx, char *group, char *resp)
{
	int			 ret;

	ret = add_a_group(ctx, group, resp);
	if (ret)
		return http_error(ret);

	store_config_file(ctx);

	return 0;
}

static int delete_group_request(void *ctx, char *group, char *resp)
{
	int			 ret;

	ret = del_group(ctx, group, resp);
	if (ret)
		return http_error(ret);

	store_config_file(ctx);

	return 0;
}

static int patch_group_request(void *ctx, char *group, struct mg_str *body,
			       char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	ret = update_group(ctx, group, data, resp);
	if (ret)
		return http_error(ret);

	store_config_file(ctx);

	return 0;
}

static int handle_group_requests(void *ctx, char *parts[],
				 struct http_message *hm, char *resp)
{
	char			*group;
	int			 ret;

	group = parts[1];

	if (is_equal(&hm->method, &s_get_method))
		ret = get_group_request(ctx, group, resp);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_group_request(ctx, &hm->body, resp);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_group_request(ctx, group, resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_group_request(ctx, group, resp);
	else if (is_equal(&hm->method, &s_patch_method))
		ret = patch_group_request(ctx, group, &hm->body, resp);
	else
		ret = bad_request(resp);

	return ret;
}

static int handle_host_requests(void *ctx, char *parts[],
				struct http_message *hm, char *resp)
{
	char			*group;
	char			*host;
	char			*ss;
	int			 ret;

	group = parts[1];
	host = parts[3];
	ss = parts[4];

	if (is_equal(&hm->method, &s_get_method))
		ret = get_host_request(ctx, group, host, resp);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_host_request(ctx, group, host, ss, &hm->body, resp);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_host_request(ctx, group, host, ss, resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_host_request(ctx, group, host, &hm->body, resp);
	else if (is_equal(&hm->method, &s_patch_method))
		ret = patch_host_request(ctx, group, host, ss, &hm->body, resp);
	else
		ret = bad_request(resp);

	return ret;
}

#define MAX_DEPTH 5

void handle_http_request(void *ctx, struct mg_connection *c, void *ev_data)
{
	struct json_context	*context = ctx;
	struct http_message	*hm = (struct http_message *) ev_data;
	char			*resp = NULL;
	char			*uri = NULL;
	char			 error[16];
	char			*parts[MAX_DEPTH] = { NULL };
	int			 ret;

	pthread_spin_lock(&context->lock);

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

	if (parse_uri(uri, MAX_DEPTH, parts) < 0)
		goto bad_page;

	if (strncmp(parts[0], TARGET_DEM, DEM_LEN) == 0) {
		ret = handle_dem_requests(parts[1], hm, resp);
		goto out;
	}

	if (strncmp(parts[0], TARGET_GROUP, GROUP_LEN) != 0)
		goto bad_page;

	if (!parts[2])
		ret = handle_group_requests(ctx, parts, hm, resp);
	else if (strncmp(parts[2], TARGET_CTLR, CTLR_LEN) == 0)
		ret = handle_ctlr_requests(ctx, parts, hm, resp);
	else if (strncmp(parts[2], TARGET_HOST, HOST_LEN) == 0)
		ret = handle_host_requests(ctx, parts, hm, resp);
	else
		goto bad_page;

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

	pthread_spin_unlock(&context->lock);
}

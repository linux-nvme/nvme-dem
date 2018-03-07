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

#include "mongoose.h"
#include "common.h"

static const struct mg_str s_get_method = MG_MK_STR("GET");
static const struct mg_str s_put_method = MG_MK_STR("PUT");
static const struct mg_str s_post_method = MG_MK_STR("POST");
static const struct mg_str s_patch_method = MG_MK_STR("PATCH");
static const struct mg_str s_options_method = MG_MK_STR("OPTIONS");
static const struct mg_str s_delete_method = MG_MK_STR("DELETE");
static const struct mg_str s_authorization = MG_MK_STR("Authorization");
static const struct mg_str s_signature_default =
	MG_MK_STR("Basic QjU6v9wxOTU4QGBlgPztOCQ6QTsD");
struct mg_str s_signature_user;
struct mg_str *s_signature = (struct mg_str *) &s_signature_default;

#define HTTP_HDR			"HTTP/1.1"
#define SMALL_RSP			128
#define LARGE_RSP			512

#define HTTP_OK				200
#define HTTP_ERR_NOT_FOUND		402
#define HTTP_ERR_INTERNAL		403
#define HTTP_ERR_PAGE_NOT_FOUND		404
#define HTTP_ERR_NOT_IMPLEMENTED	405
#define HTTP_ERR_FORBIDDEN		403
#define HTTP_ERR_CONFLICT		409
#define HTTP_ALLOW			"Access-Control-Allow-Origin:*"
#define HTTP_ALLOW_CONTROL \
"Access-Control-Allow-Methods:GET,PUT,POST,DELETE,PATCH,OPTIONS\r\n" \
"Access-Control-Allow-Headers:" \
"access-control-allow-origin,origin,content-type,accept,x-requested-with," \
"authorization,client-security-token,accept-encoding"

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

	if (*p != '/') /* skip http:/ */
		p += 6;
	depth++;
	for (; depth && *p; p++)
		if (*p == '/') {
			*p = '\0';
			i++, depth--;
			if (depth)
				part[i] = &p[1];
		}

	return part[i] ? i + 1 : i;
}

static int get_dem_request(char *verb, char *resp)
{
	struct host_iface	*iface = interfaces;
	int			 i;
	int			 n = 0;

	if (verb && *verb)
		return bad_request(resp);

	n = sprintf(resp, "{" JSARRAY, TAG_INTERFACES);
	resp += n;

	for (i = 0; i < num_interfaces; i++, iface++) {
		n = sprintf(resp, "%s{" JSINDX ",", (i) ? "," : "", TAG_ID, i);
		resp += n;

		n = sprintf(resp, JSSTR ",", TAG_TYPE, iface->type);
		resp += n;

		n = sprintf(resp, JSSTR ",", TAG_FAMILY, iface->family);
		resp += n;

		n = sprintf(resp, JSSTR ",", TAG_ADDRESS,
			    iface->address);
		resp += n;

		n = sprintf(resp, JSSTR "}", TAG_TRSVCID,
			    iface->pseudo_target_port);
		resp += n;
	}

	sprintf(resp, "]}");

	return 0;
}

static int post_dem_request(char *verb, struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret = 0;

	if (strcmp(verb, METHOD_SHUTDOWN) == 0) {
		shutdown_dem();
		strcpy(resp, "DEM Discovery controller shutting down");
	} else if (strcmp(verb, URI_SIGNATURE) == 0) {
		memset(data, 0, sizeof(data));
		strncpy(data, body->p, min(LARGE_RSP, body->len));

		ret = update_signature(data, resp);
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
		ret = post_dem_request(verb, &hm->body, resp);
	else
		ret = bad_request(resp);

	return ret;
}

static int get_target_request(char *target, char **p, int n, char *query,
			      char **resp)
{
	int			 ret;

	if (!target || !*target) {
		if ((strncmp(query, URI_PARM_MODE, PARM_MODE_LEN) == 0)
		    && (query[PARM_MODE_LEN]))
			ret = list_json_target(query, resp);
		else if ((strncmp(query, URI_PARM_FABRIC, PARM_FABRIC_LEN) == 0)
			 && (query[PARM_FABRIC_LEN]))
			ret = list_json_target(query, resp);
		else
			ret = list_json_target(NULL, resp);
	} else if (n == 0)
		ret = show_json_target(target, resp);
	else if (n == 1 && !strcmp(*p, URI_USAGE)) {
		ret = target_usage(target, resp);
		if (ret)
			sprintf(*resp, "%s '%s' not found", TAG_TARGET, target);
	} else if (n == 1 && !strcmp(*p, URI_LOG_PAGE)) {
		ret = target_logpage(target, resp);
		if (ret)
			sprintf(*resp, "%s '%s' not found", TAG_TARGET, target);
	} else
		ret = bad_request(*resp);

	return http_error(ret);
}

static int delete_target_request(char *target, char **p, int n,
				 struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 portid;
	int			 ret;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (!n)
		ret = del_target(target, resp);
	else if (strcmp(*p, URI_SUBSYSTEM) == 0) {
		if (n == 2)
			ret = del_subsys(target, p[1], resp);
		else if (n != 4)
			goto bad_req;
		else if (strcmp(p[2], URI_NSID) == 0)
			ret = del_ns(target, p[1], atoi(p[3]), resp);
		else if (strcmp(p[2], URI_HOST) == 0)
			ret = unlink_host(target, p[1], p[3], resp);
		else
			goto bad_req;
	} else if (strcmp(*p, URI_PORTID) == 0 && n == 2) {
		portid = atoi(p[1]);
		if (!portid)
			goto bad_req;
		ret = del_portid(target, portid, resp);
	} else {
bad_req:
		bad_request(resp);
		ret = -EINVAL;
	}

	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int put_target_request(char *target, char **p, int n,
			      struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 portid;
	int			 ret;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (n == 0)
		ret = update_target(target, data, resp);
	else if (strcmp(*p, URI_INTERFACE) == 0)
		ret = set_interface(target, data, resp);
	else if (strcmp(*p, URI_SUBSYSTEM) == 0) {
		if (n <= 2)
			ret = set_subsys(target, p[1], data, resp);
		else if (n <= 4) {
			if (strcmp(p[2], URI_NSID) == 0)
				ret = set_ns(target, p[1], data, resp);
			else if (strcmp(p[2], URI_HOST) == 0)
				ret = link_host(target, p[1], p[3], data, resp);
			else
				goto bad_req;
		} else
			goto bad_req;
	} else if (strcmp(*p, URI_PORTID) == 0 && n <= 2) {
		if (n == 1)
			portid = 0;
		else {
			portid = atoi(p[1]);
			if (!portid)
				goto bad_req;
		}
		ret = set_portid(target, portid, data, resp);
	} else {
bad_req:
		bad_request(resp);
		ret = -EINVAL;
	}

	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int post_target_request(char *target, char **p, int n,
			       struct mg_str *body, char *resp)
{
	char			 data[SMALL_RSP + 1];
	int			 ret;

	if (body->len) {
		memset(data, 0, sizeof(data));
		strncpy(data, body->p, min(LARGE_RSP, body->len));

		if (n) {
			if (strcmp(*p, URI_SUBSYSTEM) == 0)
				ret = set_subsys(target,
						 p[1], data, resp);
			else
				ret = -EINVAL;

		} else
			ret = update_target(target, target, resp);
	} else if (!n)
		ret = add_target(target, resp);
	else if (!strcmp(*p, METHOD_RECONFIG)) {
		ret = target_reconfig(target);
		if (ret) {
			sprintf(resp, "%s '%s' not found", TAG_TARGET, target);
			ret = HTTP_ERR_INTERNAL;
		} else
			sprintf(resp, "%s '%s' reconfigured",
				TAG_TARGET, target);
	} else if (!strcmp(*p, METHOD_REFRESH)) {
		ret = target_refresh(target);
		if (ret) {
			sprintf(resp, "%s '%s' not found", TAG_TARGET, target);
			ret = HTTP_ERR_INTERNAL;
		} else
			sprintf(resp, "%s '%s' refreshed", TAG_TARGET, target);
	} else { // check for the value of n well.
		if (strcmp(*p, URI_SUBSYSTEM) == 0) {
			sprintf(data, "{" JSSTR "," JSINDX "}",
				TAG_SUBNQN, p[1], TAG_ALLOW_ANY, 1);
			ret = set_subsys(target, NULL, data, resp);
		} else  //check if its portid
			ret = -EINVAL;
	}

	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int patch_target_request(char *target, char **p, int n,
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

	if (n) {
		if (strcmp(*p, URI_SUBSYSTEM) == 0)
			ret = set_subsys(target, p[1], data, resp);
		else
			ret = -EINVAL;
	} else
		ret = update_target(target, data, resp);

out:
	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int handle_target_requests(char *p[], int n, struct http_message *hm,
				  char **resp)
{
	char			*target;
	char			 query[32] = { 0 };
	int			 ret;

	target = p[1];
	p += 2;
	n = (n > 2) ? n - 2 : 0;

	if (hm->query_string.len)
		strncpy(query, hm->query_string.p,
			min(hm->query_string.len, sizeof(query) - 1));

	if (is_equal(&hm->method, &s_get_method))
		ret = get_target_request(target, p, n, query, resp);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_target_request(target, p, n, &hm->body, *resp);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_target_request(target, p, n, &hm->body, *resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_target_request(target, p, n, &hm->body, *resp);
	else if (is_equal(&hm->method, &s_patch_method))
		ret = patch_target_request(target, p, n, &hm->body, *resp);
	else
		ret = bad_request(*resp);

	return ret;
}

static int get_host_request(char *host, char **p, int n, char **resp)
{
	int			 ret;

	if (!host)
		ret = list_json_host(resp);
	else if (n == 0)
		ret = show_json_host(host, resp);
	else if (n == 1 && !strcmp(*p, URI_LOG_PAGE)) {
		ret = host_logpage(host, resp);
		if (ret)
			sprintf(*resp, "%s '%s' not found", TAG_HOST, host);
	}

	return http_error(ret);
}

static int delete_host_request(char *host, int n, char *resp)
{
	int			 ret;

	if (n) {
		bad_request(resp);
		return http_error(-EINVAL);
	}

	ret = del_host(host, resp);
	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int put_host_request(char *host, int n, struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret;

	if (n) {
		bad_request(resp);
		return http_error(-EINVAL);
	}

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	ret = update_host(host, data, resp);
	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int post_host_request(char *host, int n, struct mg_str *body, char *resp)
{
	char			 data[SMALL_RSP + 1];
	int			 ret;

	if (n)
		return http_error(-EINVAL);

	if (!body->len)
		ret = add_host(host, resp);
	else {
		memset(data, 0, sizeof(data));
		strncpy(data, body->p, min(LARGE_RSP, body->len));

		ret = update_host(host, data, resp);
	}

	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int patch_host_request(char *host, char **p, int n, struct mg_str *body,
			      char *resp)
{
	char			 data[SMALL_RSP + 1];
	int			 ret;

	UNUSED(p);

	if (!body->len) {
		sprintf(resp, "no data provided");
		ret = -EINVAL;
		goto out;
	}

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (host && !n)
		ret = update_host(host, data, resp);
	else {
		bad_request(resp);
		ret = -EINVAL;
	}

out:
	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int get_group_request(char *group, char **resp)
{
	int			 ret;

	if (!group)
		ret = list_json_group(resp);
	else
		ret = show_json_group(group, resp);

	return http_error(ret);
}
static int put_group_request(char *group, char **p, int n, struct mg_str *body,
			     char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (n <= 2)
		ret = update_group(group, data, resp);
	else if (strcmp(*p, URI_HOST) == 0)
		ret = set_group_member(group, data, NULL, TAG_HOST,
				       TAG_HOSTS, resp);
	else if (strcmp(*p, URI_TARGET) == 0)
		ret = set_group_member(group, data, NULL, TAG_TARGET,
				       TAG_TARGETS, resp);
	else
		ret = bad_request(resp);

	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int post_group_request(char *group, char **p, int n, char *resp)
{
	int			 ret;

	if (n <= 2)
		ret = add_group(group, resp);
	else if (n == 4 && strcmp(*p, URI_HOST) == 0)
		ret = set_group_member(group, NULL, *++p, TAG_HOST,
				       TAG_HOSTS, resp);
	else if (n == 4 && strcmp(*p, URI_TARGET) == 0)
		ret = set_group_member(group, NULL, *++p, TAG_TARGET,
				       TAG_TARGETS, resp);
	else
		ret = bad_request(resp);

	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int delete_group_request(char *group, char **p, int n, char *resp)
{
	int			 ret;

	if (n == 2)
		ret = del_group(group, resp);
	else if (strcmp(*p, URI_HOST) == 0)
		ret = del_group_member(group, p[1], TAG_HOST, TAG_HOSTS, resp);
	else if (strcmp(*p, URI_TARGET) == 0)
		ret = del_group_member(group, p[1], TAG_TARGET, TAG_TARGETS,
				       resp);
	else
		ret = bad_request(resp);

	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int patch_group_request(char *group, struct mg_str *body, char *resp)
{
	char			 data[LARGE_RSP + 1];
	int			 ret;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	ret = update_group(group, data, resp);
	if (ret)
		return http_error(ret);

	store_json_config_file();

	return 0;
}

static int handle_group_requests(char *p[], int n, struct http_message *hm,
				 char **resp)
{
	char			*group;
	int			 ret;

	UNUSED(n);

	group = p[1];
	p += 2;

	if (is_equal(&hm->method, &s_get_method))
		ret = get_group_request(group, resp);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_group_request(group, p, n, &hm->body, *resp);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_group_request(group, p, n, *resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_group_request(group, p, n, *resp);
	else if (is_equal(&hm->method, &s_patch_method))
		ret = patch_group_request(group, &hm->body, *resp);
	else
		ret = bad_request(*resp);

	return ret;
}

static int handle_host_requests(char *p[], int n, struct http_message *hm,
				char **resp)
{
	char			*host = NULL;
	int			 ret;

	host = p[1];
	p += 2;
	n = (n > 2) ? n - 2 : 0;

	if (is_equal(&hm->method, &s_get_method))
		ret = get_host_request(host, p, n, resp);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_host_request(host, n, &hm->body, *resp);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_host_request(host, n, *resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_host_request(host, n, &hm->body, *resp);
	else if (is_equal(&hm->method, &s_patch_method))
		ret = patch_host_request(host, p, n, &hm->body, *resp);
	else
		ret = bad_request(*resp);

	return ret;
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
	int			 i, n;

	json_spinlock();

	if (!hm->uri.len) {
		strcpy(resp, "Bad page no uri");
		strcpy(error, "Page Not Found");
		ret = HTTP_ERR_PAGE_NOT_FOUND;
		goto out;
	}

	resp = malloc(BODY_SIZE);
	if (!resp) {
		strcpy(resp, "No memory!");
		strcpy(error, "No Memory");
		ret = HTTP_ERR_INTERNAL;
		goto out;
	}

	if (is_equal(&hm->method, &s_options_method)) {
		ret = -1;
		goto out;
	}

	print_debug("%.*s %.*s", (int) hm->method.len, hm->method.p,
		    (int) hm->uri.len, hm->uri.p);

	for (i = 0; i < MG_MAX_HTTP_HEADERS; i++)
		if (is_equal(&hm->header_names[i], &s_authorization))
			break;

	if ((i < MG_MAX_HTTP_HEADERS) &&
	    (!is_equal(&hm->header_values[i], s_signature))) {
		ret = HTTP_ERR_FORBIDDEN;
		goto out;
	}

	if (hm->body.len)
		print_debug("%.*s", (int) hm->body.len, hm->body.p);

	memset(resp, 0, BODY_SIZE);

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

	if (strncmp(parts[0], URI_DEM, DEM_LEN) == 0)
		ret = handle_dem_requests(parts[1], hm, resp);
	else if (strncmp(parts[0], URI_GROUP, GROUP_LEN) == 0)
		ret = handle_group_requests(parts, n, hm, &resp);
	else if (strncmp(parts[0], URI_HOST, HOST_LEN) == 0)
		ret = handle_host_requests(parts, n, hm, &resp);
	else if (strncmp(parts[0], URI_TARGET, TARGET_LEN) == 0)
		ret = handle_target_requests(parts, n, hm, &resp);
	else
		goto bad_page;

	goto out;

bad_page:
	sprintf(resp, "Bad page %.*s", (int) hm->uri.len, hm->uri.p);
	strcpy(error, "Page Not Found");
	ret = HTTP_ERR_PAGE_NOT_FOUND;
out:
	if (!ret)
		mg_printf(c, "%s %d OK\r\n%s", HTTP_HDR, HTTP_OK, HTTP_ALLOW);
	else if (ret == -1)
		mg_printf(c, "%s %d OK\r\n%s\r\n%s", HTTP_HDR, HTTP_OK,
			  HTTP_ALLOW, HTTP_ALLOW_CONTROL);
	else
		mg_printf(c, "%s %d\r\n%s\r\n%s", HTTP_HDR, ret, resp,
			  HTTP_ALLOW);

	mg_printf(c, "\r\nContent-Type: plain/text");
	mg_printf(c, "\r\nContent-Length: %ld\r\n", strlen(resp));
	mg_printf(c, "\r\n%s\r\n\r\n", resp);

	if (uri)
		free(uri);
	if (resp)
		free(resp);

	c->flags = MG_F_SEND_AND_CLOSE;

	json_spinunlock();
}

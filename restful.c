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

int  bad_request(char *resp)
{
	strcpy(resp, "Method Not Implemented");

	return HTTP_ERR_NOT_IMPLEMENTED;
}

int get_dem_request(char *resp)
{
	struct interface	*iface = interfaces;
	int			i;
	int			n = 0;

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

int post_dem_request(struct http_message *hm, char *resp)
{
	char body[SMALL_RSP+1];
	int ret = 0;

	memset(body, 0, sizeof(body));
	strncpy(body, hm->body.p, min(SMALL_RSP, hm->body.len));

	if (strcmp(body, METHOD_SHUTDOWN) == 0) {
		shutdown_dem();
		strcpy(resp, "DEM shutting down");
	} else if (strcmp(body, METHOD_APPLY) == 0) {
		ret = restart_dem();
		if (ret < 0) {
			sprintf(resp, "DEM config apply failed %d", ret);
			ret = HTTP_ERR_INTERNAL;
			goto out;
		}
		if (ret == 0)
			strcpy(resp,
			       "DEM config applied but no interdaces defined");
		else

		strcpy(resp, "DEM config applied");
	} else {
		ret = HTTP_ERR_NOT_IMPLEMENTED;
		strcpy(resp, "Method Not Implemented");
	}
out:
	return ret;
}

int handle_dem_requests(struct http_message *hm, char *resp)
{
	int ret;

	if (is_equal(&hm->method, &s_get_method))
		ret = get_dem_request(resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_dem_request(hm, resp);
	else
		ret = bad_request(resp);

	return ret;
}

int  get_ctrl_request(void *ctx, char *ctrl, char *resp)
{
	int ret;

	if (!ctrl) {
		ret = list_ctrl(ctx, resp);
		if (ret) {
			strcpy(resp, "No Controllers configured");
			ret = HTTP_ERR_NOT_FOUND;
		}
	} else {
		ret = show_ctrl(ctx, ctrl, resp);
		if (ret) {
			sprintf(resp, "Contorller %s not found", ctrl);
			ret = HTTP_ERR_NOT_FOUND;
		}
	}

	return ret;
}

int delete_ctrl_request(void *ctx, char *ctrl, char *ss, char *resp)
{
	int ret = 0;

	if (ss) {
		ret = del_subsys(ctx, ctrl, ss);
		if (!ret)
			sprintf(resp, "Subsystem %s deleted "
				"from Controller %s", ss, ctrl);
		else {
			sprintf(resp, "Subsystem %s not found "
				"for Controller %s", ss, ctrl);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}
	} else {
		ret = del_ctrl(ctx, ctrl);
		if (!ret)
			sprintf(resp, "Controller %s deleted", ctrl);
		else {
			sprintf(resp, "Controller %s not found", ctrl);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}
	}

	store_config_file(ctx);
out:
	return ret;
}

int put_ctrl_request(void *ctx, char *ctrl, char *ss, struct mg_str *body,
		     char *resp)
{
	char data[LARGE_RSP+1];
	int ret;
	int n;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));

	if (ss) {
		ret = set_subsys(ctx, ctrl, ss, data);
		if (ret == -ENOENT) {
			sprintf(resp, "Controller %s not found", ctrl);
			goto out;
		} else if (ret == -EINVAL) {
			n = sprintf(resp, "bad json object\r\n");
			resp += n;
			n = sprintf(resp, "expect either/both elements of ");
			resp += n;
			n = sprintf(resp,
				    "{ \"%s\": \"<ss>\", \"%s\": <access> } ",
				    TAG_NQN, TAG_ACCESS);
			resp += n;
			n = sprintf(resp, "\r\ngot %s", data);
			goto out;
		}

		sprintf(resp, "Controller %s Subsytem %s updated", ctrl, ss);
	} else {
		ret = set_ctrl(ctx, ctrl, data);
		if (ret) {
			sprintf(resp, "Could not update Controller %s",
				ctrl);
			ret = HTTP_ERR_INTERNAL;
			goto out;
		}

		sprintf(resp, "Controller %s updated", ctrl);
	}

	store_config_file(ctx);
out:
	return ret;
}

int  post_ctrl_request(void *ctx, char *ctrl, char *ss, struct mg_str *body,
		       char *resp)
{
	char new[SMALL_RSP+1];
	int ret;

	if (body->len) {
		memset(new, 0, sizeof(new));
		strncpy(new, body->p, min(LARGE_RSP, body->len));

		if (ss) {
			ret = rename_subsys(ctx, ctrl, ss, new);
			if (ret) {
				sprintf(resp, "Subsystem %s not found or "
					"%s already exists for Controllor %s",
					ss, ctrl, new);
				ret = HTTP_ERR_NOT_FOUND;
				goto out;
			}
			sprintf(resp,
				"Controller %s Subsystem %s renamed to %s",
				ctrl, ss, new);
		} else {
			ret = rename_ctrl(ctx, ctrl, new);

			if (ret) {
				sprintf(resp, "Controller %s not found or "
					"%s already exists", ctrl, new);
				ret = HTTP_ERR_NOT_FOUND;
				goto out;
			}

			sprintf(resp, "Controller %s renamed to %s", ctrl, new);
		}
		store_config_file(ctx);
	} else if (!strcmp(ss, METHOD_REFRESH)) {
		ret = refresh_ctrl(ctrl);
		if (ret) {
			sprintf(resp, "Controller %s not found", ctrl);
			ret = HTTP_ERR_INTERNAL;
			goto out;
		}

		sprintf(resp, "Controller %s refreshed", ctrl);
	} else
		ret = bad_request(resp);
out:
	return ret;
}

int handle_ctrl_requests(void *ctx, struct http_message *hm, char *resp)
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
		ret = get_ctrl_request(ctx, ctrl, resp);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_ctrl_request(ctx, ctrl, ss, &hm->body, resp);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_ctrl_request(ctx, ctrl, ss, resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_ctrl_request(ctx, ctrl, ss, &hm->body, resp);
	else
		ret = bad_request(resp);

	return ret;
}

int get_host_request(void *ctx, char *host, char *resp)
{
	int ret;

	if (!host) {
		ret = list_host(ctx, resp);
		if (ret) {
			strcpy(resp, "No Host configured");
			ret = HTTP_ERR_NOT_FOUND;
		}
	} else {
		ret = show_host(ctx, host, resp);
		if (ret) {
			sprintf(resp, "Host %s not found", host);
			ret = HTTP_ERR_NOT_FOUND;
		}
	}

	return ret;
}

int delete_host_request(void *ctx, char *host, char *ss, char *resp)
{
	int ret;

	if (ss) {
		ret = del_acl(ctx, host, ss);
		if (!ret)
			sprintf(resp, "Subsystem %s deleted "
				"from acl for host %s", ss, host);
		else {
			sprintf(resp, "Subsystem %s not found "
				"in acl for host %s", ss, host);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}
	} else {
		ret = del_host(ctx, host);
		if (!ret)
			sprintf(resp, "Host %s deleted", host);
		else {
			sprintf(resp, "Host %s not found", host);
			ret = HTTP_ERR_NOT_FOUND;
			goto out;
		}
	}

	store_config_file(ctx);
out:
	return ret;
}

int put_host_request(void *ctx, char *host, char *ss, struct mg_str *body,
		     char *resp)
{
	char data[LARGE_RSP+1];
	int ret;
	int n;

	if (ss) {
		memset(data, 0, sizeof(data));
		strncpy(data, body->p, min(LARGE_RSP, body->len));

		ret = set_acl(ctx, host, ss, data);
		if (!ret)
			sprintf(resp, "Subsystem %s updated to acl for host %s",
				ss, host);
		else if (ret == -ENOENT) {
			sprintf(resp, "Host %s not found", host);
			goto out;
		} else {
			n = sprintf(resp, "bad json object\r\n");
			resp += n;
			n = sprintf(resp, "expect either/both elements of ");
			resp += n;
			n = sprintf(resp,
				    "{ \"%s\": \"<ss>\", \"%s\": <access> } ",
				    TAG_NQN, TAG_ACCESS);
			resp += n;
			n = sprintf(resp, "\r\ngot %s", data);
		}
	} else {
		ret = set_host(ctx, host);
		if (!ret)
			sprintf(resp, "Host %s added", host);
		else {
			sprintf(resp, "Host %s exists", host);
			ret = HTTP_ERR_INTERNAL;
			goto out;
		}
	}

	store_config_file(ctx);
out:
	return ret;
}

int post_host_request(void *ctx, char *host, char *ss, struct mg_str *body,
		      char *resp)
{
	char new[SMALL_RSP+1];
	int ret;

	memset(new, 0, sizeof(new));
	strncpy(new, body->p, min(LARGE_RSP, body->len));

	if (ss) {
		ret = bad_request(resp);
		goto out;
	}

	ret = rename_host(ctx, host, new);
	if (ret) {
		sprintf(resp, "Host %s not found or "
			"Host %s already exists", host, new);
		ret = HTTP_ERR_NOT_FOUND;
		goto out;
	}

	sprintf(resp, "Host %s renamed to %s", host, new);

	store_config_file(ctx);
out:
	return ret;
}

int handle_host_requests(void *ctx, struct http_message *hm, char *resp)
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
		ret = get_host_request(ctx, host, resp);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_host_request(ctx, host, ss, &hm->body, resp);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_host_request(ctx, host, ss, resp);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_host_request(ctx, host, ss, &hm->body, resp);
	else
		ret = bad_request(resp);

	return ret;
}

void handle_http_request(void *ctx, struct mg_connection *c, void *ev_data)
{
	struct http_message *hm = (struct http_message *) ev_data;
	char *target = (char *) &hm->uri.p[1];
	char *resp;
	int ret;

	if (!hm->uri.len) {
		mg_printf(c, "HTTP/1.1 %d Page Not Found\r\n\r\n",
			  HTTP_ERR_PAGE_NOT_FOUND);
		goto out1;
	}

	resp = malloc(BODY_SZ);
	if (!resp) {
		fprintf(stderr, "no memory!\n");
		mg_printf(c, "HTTP/1.1 %d No Memory\r\n\r\n",
			  HTTP_ERR_INTERNAL);
		goto out1;
	}

	print_debug("%.*s %.*s", (int) hm->method.len, hm->method.p,
		(int) hm->uri.len, hm->uri.p);
	if (hm->body.len)
		print_debug("%.*s", (int) hm->body.len, hm->body.p);

	memset(resp, 0, BODY_SZ);

	if (strncmp(target, TARGET_DEM, DEM_LEN) == 0)
		ret = handle_dem_requests(hm, resp);
	else if (strncmp(target, TARGET_CTRL, CTRL_LEN) == 0)
		ret = handle_ctrl_requests(ctx, hm, resp);
	else if (strncmp(target, TARGET_HOST, HOST_LEN) == 0)
		ret = handle_host_requests(ctx, hm, resp);
	else {
		fprintf(stderr, "Bad page %*s\n", (int) hm->uri.len, hm->uri.p);
		mg_printf(c, "HTTP/1.1 %d Page Not Found\r\n\r\n",
			  HTTP_ERR_PAGE_NOT_FOUND);
		goto out2;
	}

	if (!ret)
		mg_printf(c, "HTTP/1.1 %d OK\r\n", HTTP_OK);
	else
		mg_printf(c, "HTTP/1.1 %d %s\r\n", ret, resp);

	mg_printf(c, "Content-Length: %ld\r\n\r\n%s",
		  strlen(resp), resp);
out2:
	free(resp);
out1:
	c->flags = MG_F_SEND_AND_CLOSE;
}

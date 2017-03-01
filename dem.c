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
#include <pthread.h>

#include "json.h"


/*For setting server options - e.g., SSL, document root, ...*/
static struct mg_serve_http_opts s_http_server_opts;

static int s_sig_num;

static const struct mg_str s_get_method = MG_MK_STR("GET");
static const struct mg_str s_put_method = MG_MK_STR("PUT");
static const struct mg_str s_post_method = MG_MK_STR("POST");
static const struct mg_str s_patch_method = MG_MK_STR("PATCH");
static const struct mg_str s_option_method = MG_MK_STR("OPTION");
static const struct mg_str s_delete_method = MG_MK_STR("DELETE");

void *ctx; // json context

#define min(x, y) ((x < y) ? x : y)
#define SMALL_RSP 128
#define LARGE_RSP 512
#define BODY_SZ   1024

#define HTTP_ERR_NOT_FOUND 402
#define HTTP_ERR_NOT_IMPLEMENTED 405
#define HTTP_OK 200
#define HTTP_ERR_INTERNAL 403
#define HTTP_ERR_PAGE_NOT_FOUND 404

static void signal_handler(int sig_num)
{
	signal(sig_num, signal_handler);
	s_sig_num = sig_num;
}

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
	strcpy(response, "Config of DEM");

	return 0;
}

int post_dem_request(struct http_message *hm, char *response)
{
	char body[SMALL_RSP+1];
	int ret = 0;

	memset(body, 0, sizeof(body));
	strncpy(body, hm->body.p, min(SMALL_RSP, hm->body.len));

	if (strcmp(body, "shutdown") == 0) {
		s_sig_num = 1;
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

int delete_ctrl_request(char *ctrl, char *ss, char *response)
{
	int ret = 0;

	if (ss) {
		ret = del_subsys(ctx, ctrl, ss);
		if (!ret)
			sprintf(response, "Subsystem %s not found "
				"for Controller %s", ss, ctrl);
		else {
			sprintf(response, "Subsystem %s deleted "
				"from Controller %s", ss, ctrl);
			ret = HTTP_ERR_NOT_FOUND;
		}
	} else {
		ret = del_ctrl(ctx, ctrl);
		if (!ret)
			sprintf(response, "Controller %s deleted",
				ctrl);
		else {
			sprintf(response, "Controller %s not found",
				ctrl);
			ret = HTTP_ERR_NOT_FOUND;
		}
	}
	if (!ret)
		store_config_file(ctx);

	return ret;
}

int  get_ctrl_request(char *ctrl, char *response)
{
	if (!ctrl)
		list_ctrl(ctx, response);
	else
		show_ctrl(ctx, ctrl, response);

	return 0;
}

int put_ctrl_request(char *ctrl, struct mg_str *body, char *response)
{
	char data[LARGE_RSP+1];
	int ret = 0;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));
	printf("put data: %s\n", data);
	sprintf(response, "TODO add put method for controller %s", ctrl);

	return ret;
}

int  post_ctrl_request(char *ctrl, struct mg_str *body, char *response)
{
	char data[LARGE_RSP+1];
	int ret = 0;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));
	printf("post data: %s\n", data);
	sprintf(response, "TODO add post method for controller %s", ctrl);

	return ret;
}

int handle_ctrl_requests(struct http_message *hm, char *response)
{
	char url[SMALL_RSP+1];
	char *ctrl = NULL;
	char *ss = NULL;
	char *p;
	int ret;

	memset(url, 0, sizeof(url));
	strncpy(url, hm->uri.p, min(SMALL_RSP, hm->uri.len));
	strtok_r(url, "/", &p); // checked before calling handler
	ctrl = strtok_r(NULL, "/", &p);
	if (ctrl)
		ss = strtok_r(NULL, "/", &p);

	if (is_equal(&hm->method, &s_get_method))
		ret = get_ctrl_request(ctrl, response);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_ctrl_request(ctrl, &hm->body, response);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_ctrl_request(ctrl, ss, response);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_ctrl_request(ctrl, &hm->body, response);
	else
		ret = bad_request(response);

	return ret;
}

int get_host_request(char *host, char *response)
{
	if (!host)
		list_host(ctx, response);
	else
		show_host(ctx, host, response);

	return 0;
}

int delete_host_request(char *host, char *ss, char *response)
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
		}
	} else {
		ret = del_host(ctx, host);
		if (!ret)
			sprintf(response, "Host %s deleted", host);
		else {
			sprintf(response, "Host %s not found", host);
			ret = HTTP_ERR_NOT_FOUND;
		}
	}

	if (!ret)
		store_config_file(ctx);

	return ret;
}

int put_host_request(char *host, char *ss, char *response)
{
	sprintf(response, "TODO add put method for host %s ss %s", host, ss);

	return 0;
}

int post_host_request(char *host, char *ss, struct mg_str *body, char *response)
{
	char data[LARGE_RSP+1];
	int ret = 0;

	memset(data, 0, sizeof(data));
	strncpy(data, body->p, min(LARGE_RSP, body->len));
	printf("post data: %s\n", data);
	sprintf(response, "TODO add post method for host %s ss %s", host, ss);

	return ret;
}

int handle_host_requests(struct http_message *hm, char *response)
{
	char url[SMALL_RSP+1];
	char *host = &url[5];
	char *ss = NULL;
	char *p;
	int ret;

	memset(url, 0, sizeof(url));
	strncpy(url, hm->uri.p, min(SMALL_RSP, hm->uri.len));
	strtok_r(url, "/", &p); // checked before calling handler
	host = strtok_r(NULL, "/", &p);
	if (host)
		ss = strtok_r(NULL, "/", &p);

	if (is_equal(&hm->method, &s_get_method))
		ret = get_host_request(host, response);
	else if (is_equal(&hm->method, &s_put_method))
		ret = put_host_request(host, ss, response);
	else if (is_equal(&hm->method, &s_delete_method))
		ret = delete_host_request(host, ss, response);
	else if (is_equal(&hm->method, &s_post_method))
		ret = post_host_request(host, ss, &hm->body, response);
	else
		ret = bad_request(response);

	return ret;
}

static void handle_http_request(struct mg_connection *c, void *ev_data)
{
	struct http_message *hm = (struct http_message *) ev_data;
	char *response;
	int ret;

	response = malloc(BODY_SZ);
	if (!response) {
		fprintf(stderr, "no memory!\n");
		mg_printf(c, "HTTP/1.1 %d No Memory\r\n\r\n",
			  HTTP_ERR_INTERNAL);
		goto out1;
	}

	memset(response, 0, BODY_SZ);

	if (strncmp(hm->uri.p, "/dem", 4) == 0)
		ret = handle_dem_requests(hm, response);
	else if (strncmp(hm->uri.p, "/controller", 11) == 0)
		ret = handle_ctrl_requests(hm, response);
	else if (strncmp(hm->uri.p, "/host", 5) == 0)
		ret = handle_host_requests(hm, response);
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

static void ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		handle_http_request(c, ev_data);
		break;
	case MG_EV_HTTP_CHUNK:
	case MG_EV_ACCEPT:
	case MG_EV_CLOSE:
	case MG_EV_POLL:
	case MG_EV_SEND:
	case MG_EV_RECV:
		break;
	default:
		fprintf(stderr, "ev_handler: Unexpected request %d\n", ev);
	}
}

void poll_loop(void *p)
{
	struct mg_mgr *mgr = p;

	s_sig_num = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	while (s_sig_num == 0)
		mg_mgr_poll(mgr, 1000);

	mg_mgr_free(mgr);
}

int main(int argc, char *argv[])
{
	struct mg_mgr		 mgr;
	struct mg_connection	*c;
	struct mg_bind_opts	 bind_opts;
	int			 opt;
	const char		*err_str;
	char			*cp;
	char			*s_http_port = "12345";
#if MG_ENABLE_SSL
	const char		*ssl_cert = NULL;
#endif

	mg_mgr_init(&mgr, NULL);

	s_http_server_opts.document_root = NULL;

	/* Process CLI options for HTTP server */
	while ((opt = getopt(argc, argv, "p:s:r:")) != -1) {
		switch (opt) {
		case 'r':
			s_http_server_opts.document_root = optarg;
			break;
		case 'p':
			s_http_port = optarg;
			fprintf(stderr, "Using port %s\n", s_http_port);
			break;
		case 's':
#if MG_ENABLE_SSL
			ssl_cert = optarg;
#endif
			break;
		default:
			fprintf(stderr, "Usage: %s %s\n", argv[0],
				"[-r {root}] [-p port] [-s ssl_cert]\n");
			exit(1);
		}
	}

	/* Use current binary directory as document root */
	if (!s_http_server_opts.document_root) {
		cp = strrchr(argv[0], DIRSEP);
		if (cp != NULL) {
			*cp = '\0';
			s_http_server_opts.document_root = argv[0];
		}
	}

	/* Set HTTP server options */
	memset(&bind_opts, 0, sizeof(bind_opts));
	bind_opts.error_string = &err_str;

#if MG_ENABLE_SSL
	if (ssl_cert != NULL)
		bind_opts.ssl_cert = ssl_cert;
#endif
	c = mg_bind_opt(&mgr, s_http_port, ev_handler, bind_opts);
	if (c == NULL) {
		fprintf(stderr, "Error starting server on port %s: %s\n",
			s_http_port, *bind_opts.error_string);
		exit(1);
	}

	mg_set_protocol_http_websocket(c);

	ctx = init_json("config.json");
	if (!ctx)
		return 1;

	printf("Starting server on port %s, serving %s\n",
		s_http_port, s_http_server_opts.document_root);

	poll_loop(&mgr);

	cleanup_json(ctx);

	return 0;
}

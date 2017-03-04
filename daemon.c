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

#include "common.h"

/*For setting server options - e.g., SSL, document root, ...*/
static struct mg_serve_http_opts s_http_server_opts;

static void *json_ctx;
static int s_sig_num;
static int poll_timeout = 1000;

void shutdown_dem()
{
	s_sig_num = 1;
}

static void signal_handler(int sig_num)
{
	signal(sig_num, signal_handler);
	s_sig_num = sig_num;
	printf("\n");
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		handle_http_request(json_ctx, c, ev_data);
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

static void poll_loop(void *p)
{
	struct mg_mgr *mgr = p;

	s_sig_num = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	while (s_sig_num == 0)
		mg_mgr_poll(mgr, poll_timeout);

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

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto help;

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
help:
			fprintf(stderr, "Usage: %s %s\n", argv[0],
				"{-r <root>} {-p <port>} {-s <ssl_cert>}\n");
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

	json_ctx = init_json("config.json");
	if (!json_ctx)
		return 1;

	printf("Starting server on port %s, serving %s\n",
		s_http_port, s_http_server_opts.document_root);

	poll_loop(&mgr);

	cleanup_json(json_ctx);

	return 0;
}

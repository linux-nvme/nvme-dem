/*
 * NVMe over Fabrics Distributed Endpoint Manager (NVMe-oF DEM).
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

#define _GNU_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cm.h>

#include "mongoose.h"
#include "common.h"
#include "tags.h"

#define RETRY_COUNT	200
#define DEFAULT_ROOT	"/"
#define DEMT_PORT	"22334"

#define DEV_DEBUG

#define NVME_VER ((1 << 16) | (2 << 8) | 1) /* NVMe 1.2.1 */

static LIST_HEAD(device_list_head);
static LIST_HEAD(interface_list_head);

static struct mg_serve_http_opts	 s_http_server_opts;
static char				*s_http_port = DEMT_PORT;
int					 stopped;
int					 debug;
static int				 signalled;
struct list_head			*devices = &device_list_head;
struct list_head			*interfaces = &interface_list_head;

void shutdown_dem(void)
{
	stopped = 1;
}

static void signal_handler(int sig_num)
{
	signalled = sig_num;

	shutdown_dem();
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
		print_err("unexpected request %d", ev);
	}
}

static void *poll_loop(struct mg_mgr *mgr)
{
	while (stopped != 1)
		mg_mgr_poll(mgr, IDLE_TIMEOUT);

	mg_mgr_free(mgr);

	return NULL;
}

static int daemonize(void)
{
	pid_t			 pid, sid;

	pid = fork();
	if (pid < 0) {
		print_err("fork failed %d", pid);
		return pid;
	}

	if (pid) /* if parent, exit to allow child to run as daemon */
		exit(0);

	umask(0022);

	sid = setsid();
	if (sid < 0) {
		print_err("setsid failed %d", sid);
		return sid;
	}

	if ((chdir("/")) < 0) {
		print_err("could not change dir to /");
		return -1;
	}

	freopen("/var/log/demt_debug.log", "a", stdout);
	freopen("/var/log/demt.log", "a", stderr);

	return 0;
}

static int init_dem(int argc, char *argv[], char **ssl_cert)
{
	int			 opt;
	int			 run_as_daemon;
#ifdef DEV_DEBUG
	const char		*opt_list = "?qdp:r:c:";
	const char		*arg_list =
		"{-q} {-d} {-p <port>} {-r <root>} {-c <cert_file>}\n"
		"-q - quite mode, no debug prints\n"
		"-d - run as a daemon process (default is standalone)\n"
		"-p - port from RESTful interface (default " DEMT_PORT ")\n"
		"-r - root for RESTful interface (default " DEFAULT_ROOT ")\n"
		"-c - cert file for RESTful interface use with ssl";
#else
	const char		*opt_list = "?dsp:r:c:";
	const char		*arg_list =
		"{-d} {-s} {-r <root>} {-r <root>} {-c <cert_file>}\n"
		"-d - enable debug prints in log files\n"
		"-s - run as a standalone process (default is daemon)\n"
		"-p - port from RESTful interface (default " DEMT_PORT ")\n"
		"-r - root for RESTful interface (default " DEFAULT_ROOT ")\n"
		"-c - cert file for RESTful interface use with ssl";
#endif

	*ssl_cert = NULL;

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto help;

#ifdef DEV_DEBUG
	debug = 1;
	run_as_daemon = 0;
#else
	debug = 0;
	run_as_daemon = 1;
#endif

	/* Process CLI options for HTTP server */
	while ((opt = getopt(argc, argv, opt_list)) != -1) {
		switch (opt) {
#ifdef DEV_DEBUG
		case 'q':
			debug = 0;
			break;
		case 'd':
			run_as_daemon = 1;
			break;
#else
		case 'd':
			debug = 0;
			break;
		case 's':
			run_as_daemon = 1;
			break;
#endif
		case 'r':
			s_http_server_opts.document_root = optarg;
			break;
		case 'p':
			s_http_port = optarg;
			print_info("Using port %s", s_http_port);
			break;
		case 'c':
			*ssl_cert = optarg;
			break;
		case '?':
		default:
help:
			print_info("Usage: %s %s", argv[0], arg_list);
			return 1;
		}
	}

	if (run_as_daemon) {
		if (daemonize())
			return 1;
	}

	return 0;
}

static int init_mg_mgr(struct mg_mgr *mgr, char *prog, char *ssl_cert)
{
	struct mg_bind_opts	 bind_opts;
	struct mg_connection	*c;
	char			*cp;
	const char		*err_str;

	mg_mgr_init(mgr, NULL);

	/* Use current binary directory as document root */
	if (!s_http_server_opts.document_root) {
		cp = strrchr(prog, DIRSEP);
		if (cp != NULL) {
			*cp = '\0';
			s_http_server_opts.document_root = prog;
		}
	}

	/* Set HTTP server options */
	memset(&bind_opts, 0, sizeof(bind_opts));
	bind_opts.error_string = &err_str;

#ifdef SSL_CERT
	if (ssl_cert != NULL)
		bind_opts.ssl_cert = ssl_cert;
#else
	(void) ssl_cert;
#endif

	c = mg_bind_opt(mgr, s_http_port, ev_handler, bind_opts);
	if (c == NULL) {
		print_err("failed to start server on port %s: %s",
			  s_http_port, *bind_opts.error_string);
		return 1;
	}

	mg_set_protocol_http_websocket(c);

	return 0;
}

int main(int argc, char *argv[])
{
	struct mg_mgr		 mgr;
	char			*ssl_cert = NULL;
	char			 default_root[] = DEFAULT_ROOT;
	int			 ret = 1;

	if (getuid() != 0) {
		print_info("must be root to allow access to configfs");
		return -1;
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	s_http_server_opts.document_root = default_root;

	if (init_dem(argc, argv, &ssl_cert))
		goto out1;

	if (init_mg_mgr(&mgr, argv[0], ssl_cert))
		goto out1;

	signalled = stopped = 0;

	print_info("Starting target daemon on port %s, serving '%s'",
		   s_http_port, s_http_server_opts.document_root);

	if (enumerate_devices() <= 0) {
		print_info("no nvme devices found");
		goto out1;
	}

	if (enumerate_interfaces() <= 0) {
		print_info("no interfaces found");
		goto out2;
	}

	poll_loop(&mgr);

	if (signalled)
		printf("\n");

	ret = 0;

	free_interfaces();
out2:
	free_devices();
out1:
	return ret;
}

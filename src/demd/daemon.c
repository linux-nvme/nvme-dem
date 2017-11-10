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

#include "tags.h"
#include "mongoose.h"
#include "common.h"

#define RETRY_COUNT	200
#define DEFAULT_ROOT	"/"

#define DEV_DEBUG

#define NVME_VER ((1 << 16) | (2 << 8) | 1) /* NVMe 1.2.1 */

static LIST_HEAD(target_list_head);

static struct mg_serve_http_opts	 s_http_server_opts;
static char				*s_http_port = DEFAULT_PORT;
void					*json_ctx;
int					 stopped;
int					 debug;
struct interface			*interfaces;
int					 num_interfaces;
struct list_head			*target_list = &target_list_head;
static pthread_t			*listen_threads;
static int				 signalled;

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
		print_err("unexpected request %d", ev);
	}
}

static inline void refresh_log_pages(void)
{
	struct target		*target;
	int			 ret;

	list_for_each_entry(target, target_list, node) {
		if (!target->dq_connected)
			continue;

		if (target->kato_countdown == 0) {
			ret = send_keep_alive(&target->dq);
			if (ret) {
				print_err("keep alive failed. disconnected %s",
					  target->alias);
				disconnect_target(&target->dq, 0);
				target->dq_connected = 0;
				continue;
			}
			target->kato_countdown = MINUTES / IDLE_TIMEOUT;
		} else
			target->kato_countdown--;

		if (!target->refresh)
			continue;

		target->refresh_countdown--;
		if (!target->refresh_countdown) {
			fetch_log_pages(target);
			target->refresh_countdown =
				target->refresh * MINUTES / IDLE_TIMEOUT;
		}
	}
}

static void *poll_loop(struct mg_mgr *mgr)
{
	while (stopped != 1) {
		mg_mgr_poll(mgr, IDLE_TIMEOUT);

		if (!stopped)
			refresh_log_pages();
	}

	mg_mgr_free(mgr);

	return NULL;
}

static int daemonize(void)
{
	pid_t			 pid, sid;

	if (getuid() != 0) {
		print_err("must be root to run demd as a daemon");
		return -1;
	}

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

	freopen("/var/log/dem_debug.log", "a", stdout);
	freopen("/var/log/dem.log", "a", stderr);

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
		"-p - port from RESTful interface (default " DEFAULT_PORT ")\n"
		"-r - root for RESTful interface (default " DEFAULT_ROOT ")\n"
		"-c - cert file for RESTful interface use with ssl";
#else
	const char		*opt_list = "?dsp:r:c:";
	const char		*arg_list =
		"{-d} {-s} {-r <root>} {-r <root>} {-c <cert_file>}\n"
		"-d - enable debug prints in log files\n"
		"-s - run as a standalone process (default is daemon)\n"
		"-p - port from RESTful interface (default " DEFAULT_PORT ")\n"
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

static void cleanup_threads(pthread_t *listen_threads)
{
	int			i;

	for (i = 0; i < num_interfaces; i++)
		pthread_kill(listen_threads[i], SIGTERM);

	/* wait for threads to cleanup before exiting so they can properly
	 * cleanup the ofi interface. otherwise there is a race condition
	 * that shows up as an assertion from fastlock_aquire from libfabric
	 * fi_fabric_find(). retry countdown value is *arbitrary* since the
	 * threads *should* shutdown gracefully.
	 */
	i = 100;

	while (num_interfaces && i--)
		usleep(100);

	/* even thought the threads are finished, need to call join
	 * otherwize, it will not release its memory and valgrind indicates
	 * a leak
	 */

	for (i = 0; i < num_interfaces; i++)
		pthread_join(listen_threads[i], NULL);

	free(listen_threads);
}

static int init_interface_threads(pthread_t **listen_threads)
{
	pthread_attr_t		 pthread_attr;
	pthread_t		*pthreads;
	int			 i;

	pthreads = calloc(num_interfaces, sizeof(pthread_t));
	if (!pthreads)
		return -ENOMEM;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	pthread_attr_init(&pthread_attr);

	for (i = 0; i < num_interfaces; i++) {
		if (pthread_create(&pthreads[i], &pthread_attr,
				   interface_thread, &(interfaces[i]))) {
			print_err("failed to start transport thread");
			free(pthreads);
			return 1;
		}
		usleep(25); // allow new thread to start
	}

	*listen_threads = pthreads;

	return 0;
}

int restart_dem(void)
{
	int			ret = 1;

	stopped = 2;

// TODO should only cleanup/init targets that were modified.

	cleanup_targets(1);

	stopped = 0;

	init_targets(1);

	return ret;
}

int main(int argc, char *argv[])
{
	struct mg_mgr		 mgr;
	char			*ssl_cert = NULL;
	char			 default_root[] = DEFAULT_ROOT;
	int			 ret = 1;

	s_http_server_opts.document_root = default_root;

	if (init_dem(argc, argv, &ssl_cert))
		goto out1;

	if (init_mg_mgr(&mgr, argv[0], ssl_cert))
		goto out1;

	json_ctx = init_json("config.json");
	if (!json_ctx)
		goto out1;

	num_interfaces = init_interfaces();
	if (num_interfaces <= 0)
		goto out2;

	init_targets(0);

	signalled = stopped = 0;

	print_info("Starting server on port %s, serving '%s'",
		   s_http_port, s_http_server_opts.document_root);

	if (init_interface_threads(&listen_threads))
		goto out3;

	poll_loop(&mgr);

	cleanup_threads(listen_threads);

	if (signalled)
		printf("\n");

	ret = 0;
out3:
	free(interfaces);
	cleanup_targets(0);
out2:
	cleanup_json(json_ctx);
out1:
	return ret;
}

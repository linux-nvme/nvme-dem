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

#include <pthread.h>
#include <stdbool.h>

#include "../nvme/nvme-cli/nvme.h"	/*HACK: Hmmmm*/
#include "mongoose.h"
#include "common.h"

static struct mg_serve_http_opts  s_http_server_opts;
char				 *s_http_port = "22345";
static void			 *json_ctx;
static int			  s_sig_num;
static int			  poll_timeout = 100;
int				  debug;

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

static void *poll_loop(struct mg_mgr *mgr)
{
	s_sig_num = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	while (s_sig_num == 0)
		mg_mgr_poll(mgr, poll_timeout);

	mg_mgr_free(mgr);

	return NULL;
}

static int get_logpages(char *argstr, int len)
{
	char	*buf[DISC_BUF_SIZE];
	int	 fid;
	int	 ret;

	printf("argstr = %s\n", argstr);
	
	fid = open(PATH_NVME_FABRICS, O_RDWR);
	if (fid < 0) {
		fprintf(stderr, "Failed to open %s with %s\n",
				PATH_NVME_FABRICS, strerror(errno));
		ret = -errno;
		goto err1;
	}

	if (write(fid, argstr, len) != len) {
		fprintf(stderr, "Failed to write to %s with %s\n",
				PATH_NVME_FABRICS, strerror(errno));
		ret = -errno;
		goto err2;
	}
			
	len = read(fid, buf, DISC_BUF_SIZE);
	if (len < 0) {
		fprintf(stderr, "Failed to read log from %s with %s\n",
				PATH_NVME_FABRICS, strerror(errno));
		ret = -errno;
		goto err2;
	}

	/*TODO: Parse incoming log page*/
err2:
	close(fid);	
err1:	
	return ret;
}

static void *xport_loop(void *this_interface)
{
	struct interface		 *iface = (struct interface *)this_interface;
	char				  disc_argstr[DISC_BUF_SIZE];
	char				 *p = disc_argstr;
	struct controller		 *controller;
	int len;

	if (get_transport(iface, json_ctx)) {
		fprintf(stderr, "Failed to get transport for iface %d\n",
				iface->interface_id);
		return NULL;
	}

	fprintf(stderr, "interface id = %d with %d controllers\n",
			 iface->interface_id, iface->num_controllers);

	fprintf(stderr, "BEFORE while controllers loop\n");

	controller = iface->controller_list;

	/* Or should this 'for loop' on iface->num_controllers? */
	while(controller) {
		fprintf(stderr, "INSIDE while controllers loop\n");

		len = sprintf(p, "nqn=%s,transport=%s,traddr=%s", NVME_DISC_SUBSYS_NAME,
				  iface->trtype, controller->address);

		fprintf(stderr, "%s",p);
		if (get_logpages(disc_argstr, len)) {
			fprintf(stderr, "Failed to get logpage for controller %s\n",
				 controller->address);
			continue;
		}
		controller = controller->next;
	}
	fprintf(stderr, "AFTER while controllers loop\n");
	return NULL;
}

static int daemonize(void)
{
	pid_t pid, sid;

	if (getuid() != 0) {
		fprintf(stderr, "Must be root to run dem as a daemon\n");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork failed %d\n", pid);
		return pid;
	}

	if (pid) /* if parent, exit to allow child to run as daemon */
		exit(0);

	umask(0022);

	sid = setsid();
	if (sid < 0) {
		fprintf(stderr, "setsid failed %d\n", sid);
		return sid;
	}

	if ((chdir("/")) < 0) {
		fprintf(stderr, "could not change dir to /\n");
		return -1;
	}

	freopen("/var/log/dem_debug.log", "a", stdout);
	freopen("/var/log/dem.log", "a", stderr);

	return 0;
}

int init_dem(int argc, char *argv[],  char **ssl_cert)
{
	int	opt;
	int	run_as_daemon;

	*ssl_cert = NULL;

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto help;

	debug = 0;

	/* Process CLI options for HTTP server */
	while ((opt = getopt(argc, argv, "Ddp:s:r:")) != -1) {
		switch (opt) {
		case 'D':
			debug = 1;
			break;
		case 'd':
			run_as_daemon = 1;
			break;
		case 'r':
			s_http_server_opts.document_root = optarg;
			break;
		case 'p':
			s_http_port = optarg;
			fprintf(stderr, "Using port %s\n", s_http_port);
			break;
		case 's':
			*ssl_cert = optarg;
			break;
		default:
help:
			fprintf(stderr, "Usage: %s %s\n", argv[0],
				"{-r <root>} {-p <port>} {-s <ssl_cert>}\n");
			return 1;
		}
	}

	if (run_as_daemon) {
		if (daemonize())
			return 1;
	}

	return 0;
}

int init_mg_mgr(struct mg_mgr *mgr, char *prog, char *ssl_cert)
{
	struct mg_bind_opts	 bind_opts;
	struct mg_connection	*c;
	char			*cp;
	const char		*err_str;

	mg_mgr_init(mgr, NULL);
	s_http_server_opts.document_root = NULL;

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
		fprintf(stderr, "Error starting server on port %s: %s\n",
			s_http_port, *bind_opts.error_string);
		return 1;
	}

	mg_set_protocol_http_websocket(c);


	return 0;
}

void cleanup_threads(pthread_t *xport_pthread, int count)
{
	int i;

	for (i = 0; i < count; i++)
		pthread_kill(xport_pthread[i], s_sig_num);

	free(xport_pthread);
}

int init_threads(pthread_t **xport_pthread, struct interface *interfaces, int count)
{
	pthread_attr_t		 pthread_attr;
	pthread_t		*pthreads;
	int			 i;

	pthreads = calloc(count, sizeof(pthread_t));
	if (!pthreads)
		return -ENOMEM;

	s_sig_num = 0;

	signal(SIGSEGV, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	pthread_attr_init(&pthread_attr);

	for (i = 0; i < count; i++) {
		if (pthread_create(&pthreads[i], &pthread_attr, xport_loop, &(interfaces[i]))) {
			fprintf(stderr, "Error starting transport thread\n");
			free(pthreads);
			return 1;
		}
	}

	*xport_pthread = pthreads;

	return 0;
}

int main(int argc, char *argv[])
{
	struct mg_mgr		 mgr;
	char			*ssl_cert = NULL;
	pthread_t		*xport_pthread;
	struct interface	*interfaces;
	int			 num_interfaces;

	if (init_dem(argc, argv, &ssl_cert))
		return 1;

	if (init_mg_mgr(&mgr, argv[0], ssl_cert))
		return 1;

	json_ctx = init_json("config.json");
	if (!json_ctx)
		return 1;

	num_interfaces = init_interfaces(&interfaces);
	if (num_interfaces <=0)
		return 1;

	print_info("Starting server on port %s, serving '%s'",
		    s_http_port, s_http_server_opts.document_root);

	if (init_threads(&xport_pthread, interfaces, num_interfaces))
		return 1;

	poll_loop(&mgr);

	cleanup_threads(xport_pthread, num_interfaces);

	free(interfaces);

	cleanup_json(json_ctx);

	return 0;
}

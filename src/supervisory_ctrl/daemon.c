// SPDX-License-Identifier: DUAL GPL-2.0/BSD
/*
 * NVMe over Fabrics Distributed Endpoint Management (NVMe-oF DEM).
 * Copyright (c) 2017-2018 Intel Corporation, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "mongoose.h"
#include "common.h"
#include "tags.h"
#include "ops.h"

#define DEFAULT_HTTP_ROOT	"/"

static LINKED_LIST(device_linked_list);
static LINKED_LIST(interface_linked_list);

int					 stopped;
int					 debug;
static int				 signalled;
static struct mg_serve_http_opts	 s_http_server_opts;
static char				*s_http_port;
struct linked_list			*devices = &device_linked_list;
struct linked_list			*interfaces = &interface_linked_list;
static struct host_iface		 host_iface;

void shutdown_dem(void)
{
	stopped = 1;
}

static void signal_handler(int sig_num)
{
	signalled = sig_num;

	shutdown_dem();
}

void wait_for_signalled_shutdown()
{
	while (!stopped)
		usleep(100);

	if (signalled)
		printf("\n");
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

static void show_help(char *app)
{
#ifdef DEV_DEBUG
	const char		*arg_list = "{-q} {-d}";
#else
	const char		*arg_list = "{-d} {-S}";
#endif
	const char		*oob_args =
		"{-p <port>} {-r <root>} {-c <cert_file>}";
	const char		*inb_args =
		"{-t <trtype>} {-f <adrfam>} {-a <traddr>} {-s <trsvcid>}";

	print_info("Usage: %s %s", app, arg_list);
	print_info(" FOR Out-of-Band (HTTP) -- %s", oob_args);
	print_info(" FOR In-Band (SC) -- %s", inb_args);

#ifdef DEV_DEBUG
	print_info("  -q - quite mode, no debug prints");
	print_info("  -d - run as a daemon process (default is standalone)");
#else
	print_info("  -d - enable debug prints in log files");
	print_info("  -S - run as a standalone process (default is daemon)");
#endif

	print_info("  Out-of-Band (RESTful) interface:");
	print_info("  -p - port");
	print_info("  -r - http root (default %s)", DEFAULT_HTTP_ROOT);
	print_info("  -c - SSL cert file (default no SSL)");

	print_info("  In-Band (Supervisory Controller) interface:");
	print_info("  -t - transport type [ rdma, tcp. fc ]");
	print_info("  -f - address family [ ipv4, ipv6. fc ]");
	print_info("  -a - transport address");
	print_info("  -s - transport service id");
}

static int validate_host_iface(void)
{
	int			 ret = 0;

	if (strcmp(host_iface.type, TRTYPE_STR_RDMA) == 0)
		host_iface.ep.ops = rdma_register_ops();

	if (!host_iface.ep.ops)
		goto out;

	if (strcmp(host_iface.family, ADRFAM_STR_IPV4) == 0)
		host_iface.adrfam = NVMF_ADDR_FAMILY_IP4;
	else if (strcmp(host_iface.family, ADRFAM_STR_IPV6) == 0)
		host_iface.adrfam = NVMF_ADDR_FAMILY_IP6;
	else if (strcmp(host_iface.family, ADRFAM_STR_FC) == 0)
		host_iface.adrfam = NVMF_ADDR_FAMILY_FC;

	if (!host_iface.adrfam) {
		print_info("Invalid adrfam: valid options %s, %s, %s",
			   ADRFAM_STR_IPV4, ADRFAM_STR_IPV6, ADRFAM_STR_FC);
		goto out;
	}

	switch (host_iface.adrfam) {
	case NVMF_ADDR_FAMILY_IP4:
		ret = ipv4_to_addr(host_iface.address, host_iface.addr);
		break;
	case NVMF_ADDR_FAMILY_IP6:
		ret = ipv6_to_addr(host_iface.address, host_iface.addr);
		break;
	case NVMF_ADDR_FAMILY_FC:
		ret = fc_to_addr(host_iface.address, host_iface.addr);
		break;
	}

	if (ret) {
		print_info("Invalid traddr");
		goto out;
	}

	if (host_iface.port)
		host_iface.port_num = atoi(host_iface.port);

	if (!host_iface.port_num) {
		print_info("Invalid trsvcid");
		goto out;
	}

	ret = 1;
out:
	return ret;
}

static int init_dem(int argc, char *argv[], char **ssl_cert)
{
	int			 opt;
	int			 inb_test;
	int			 run_as_daemon;
#ifdef DEV_DEBUG
	const char		*opt_list = "?qdp:r:c:t:f:a:s:";
#else
	const char		*opt_list = "?dSp:r:c:t:f:a:s:";
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
			debug = 1;
			break;
		case 'S':
			run_as_daemon = 0;
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
		case 't':
			strncpy(host_iface.type, optarg, CONFIG_TYPE_SIZE);
			break;
		case 'f':
			strncpy(host_iface.family, optarg, CONFIG_FAMILY_SIZE);
			break;
		case 'a':
			strncpy(host_iface.address, optarg,
				CONFIG_ADDRESS_SIZE);
			break;
		case 's':
			strncpy(host_iface.port, optarg, CONFIG_PORT_SIZE);
			break;
		case '?':
		default:
help:
			show_help(argv[0]);
			return 1;
		}
	}

	inb_test = (host_iface.type[0]) ? 1 : 0;
	inb_test += (host_iface.family[0]) ? 1 : 0;
	inb_test += (host_iface.address[0]) ? 1 : 0;
	inb_test += (host_iface.port[0]) ? 1 : 0;

	if ((inb_test > 0) && (inb_test < 4)) {
		print_err("incomplete in-band address info");
		return 1;
	}

	if (!inb_test && !s_http_port) {
		print_err("neither in-band not out-of-band info provided");
		return 1;
	}

	if (inb_test && !validate_host_iface()) {
		print_err("invalid in-band address info");
		return 1;
	}

	if (run_as_daemon) {
		if (daemonize())
			return 1;
	}

	return 0;
}

static int init_mg_mgr(struct mg_mgr *mgr, char *ssl_cert)
{
	struct mg_bind_opts	 bind_opts;
	struct mg_connection	*c;
	const char		*err_str;

	mg_mgr_init(mgr, NULL);

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

	print_info("Starting daemon on port %s, serving '%s'",
		   s_http_port, s_http_server_opts.document_root);

	return 0;
}

static void cleanup_inb_thread(pthread_t *listen_thread)
{
	pthread_kill(*listen_thread, SIGTERM);

	/* wait for threads to cleanup before exiting so they can properly
	 * cleanup.
	 */

	usleep(100);

	/* even thought the threads are finished, need to call join
	 * otherwize, it will not release its memory and valgrind indicates
	 * a leak
	 */

	pthread_join(*listen_thread, NULL);
}

static int init_inb_thread(pthread_t *listen_thread)
{
	pthread_attr_t		 pthread_attr;
	int			 ret;

	pthread_attr_init(&pthread_attr);

	ret = pthread_create(listen_thread, &pthread_attr, interface_thread,
			     &host_iface);
	if (ret)
		print_err("failed to start thread for supervisory controller");

	pthread_attr_destroy(&pthread_attr);

	return ret;
}

int main(int argc, char *argv[])
{
	struct mg_mgr		 mgr;
	char			*ssl_cert = NULL;
	pthread_t		 inb_pthread;
	char			 default_root[] = DEFAULT_HTTP_ROOT;
	int			 ret = 1;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	s_http_server_opts.document_root = default_root;

	if (init_dem(argc, argv, &ssl_cert))
		goto out1;

	if (getuid() != 0) {
		print_info("must be root to allow access to configfs");
		goto out1;
	}

	if (s_http_port)
		if (init_mg_mgr(&mgr, ssl_cert))
			goto out1;

	signalled = stopped = 0;

	if (enumerate_devices() <= 0) {
		print_info("no nvme devices found");
		goto out1;
	}

	if (enumerate_interfaces() <= 0) {
		print_info("no interfaces found");
		goto out2;
	}

	if (host_iface.type[0])
		if (init_inb_thread(&inb_pthread))
			goto out3;

	if (s_http_port)
		poll_loop(&mgr);
	else
		wait_for_signalled_shutdown();

	ret = 0;

	if (host_iface.type[0])
		cleanup_inb_thread(&inb_pthread);

out3:
	free_interfaces();
out2:
	free_devices();
out1:
	return ret;
}

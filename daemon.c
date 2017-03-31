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
#include <sys/ioctl.h>

#include "mongoose.h"
#include "common.h"

#include "nvme_ioctl.h" /* NOTE: Using linux kernel include */

struct mg_serve_http_opts	s_http_server_opts;
char				*s_http_port = "22345";
void				*json_ctx;
int				s_sig_num;
int				stopped;
int				poll_timeout = 100;
int				debug;

static const char *arg_str(const char * const *strings, size_t array_size,
			   size_t idx)
{
	if (idx < array_size && strings[idx])
		return strings[idx];
	return "unrecognized";
}

static const char * const trtypes[] = {
	[NVMF_TRTYPE_RDMA]	= "rdma",
	[NVMF_TRTYPE_FC]	= "fibre-channel",
	[NVMF_TRTYPE_LOOP]	= "loop",
};

static const char *trtype_str(u8 trtype)
{
	return arg_str(trtypes, ARRAY_SIZE(trtypes), trtype);
}

static const char * const adrfams[] = {
	[NVMF_ADDR_FAMILY_PCI]	= "pci",
	[NVMF_ADDR_FAMILY_IP4]	= "ipv4",
	[NVMF_ADDR_FAMILY_IP6]	= "ipv6",
	[NVMF_ADDR_FAMILY_IB]	= "infiniband",
	[NVMF_ADDR_FAMILY_FC]	= "fibre-channel",
};

static inline const char *adrfam_str(u8 adrfam)
{
	return arg_str(adrfams, ARRAY_SIZE(adrfams), adrfam);
}

static const char * const subtypes[] = {
	[NVME_NQN_DISC]		= "discovery subsystem",
	[NVME_NQN_NVME]		= "nvme subsystem",
};

static inline const char *subtype_str(u8 subtype)
{
	return arg_str(subtypes, ARRAY_SIZE(subtypes), subtype);
}

static const char * const treqs[] = {
	[NVMF_TREQ_NOT_SPECIFIED]	= "not specified",
	[NVMF_TREQ_REQUIRED]		= "required",
	[NVMF_TREQ_NOT_REQUIRED]	= "not required",
};

static inline const char *treq_str(u8 treq)
{
	return arg_str(treqs, ARRAY_SIZE(treqs), treq);
}

static const char * const prtypes[] = {
	[NVMF_RDMA_PRTYPE_NOT_SPECIFIED]	= "not specified",
	[NVMF_RDMA_PRTYPE_IB]			= "infiniband",
	[NVMF_RDMA_PRTYPE_ROCE]			= "roce",
	[NVMF_RDMA_PRTYPE_ROCEV2]		= "roce-v2",
	[NVMF_RDMA_PRTYPE_IWARP]		= "iwarp",
};

static inline const char *prtype_str(u8 prtype)
{
	return arg_str(prtypes, ARRAY_SIZE(prtypes), prtype);
}

static const char * const qptypes[] = {
	[NVMF_RDMA_QPTYPE_CONNECTED]	= "connected",
	[NVMF_RDMA_QPTYPE_DATAGRAM]	= "datagram",
};

static inline const char *qptype_str(u8 qptype)
{
	return arg_str(qptypes, ARRAY_SIZE(qptypes), qptype);	}

static const char * const cms[] = {
	[NVMF_RDMA_CMS_RDMA_CM] = "rdma-cm",
};

static const char *cms_str(u8 cm)
{
	return arg_str(cms, ARRAY_SIZE(cms), cm);
}

void shutdown_dem()
{
	s_sig_num = 1;
	stopped = 1;
}

static void signal_handler(int sig_num)
{
	signal(sig_num, signal_handler);
	s_sig_num = sig_num;
	stopped = 1;
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
		print_err("ev_handler: Unexpected request %d", ev);
	}
}

static void *poll_loop(struct mg_mgr *mgr)
{
	while (!stopped)
		mg_mgr_poll(mgr, poll_timeout);

	mg_mgr_free(mgr);

	return NULL;
}

static int get_logpages(struct controller *ctrl,
			struct nvmf_disc_rsp_page_hdr **logp, u32 *numrec)
{
	struct nvmf_disc_rsp_page_hdr	*log;
	unsigned int			 log_size = 0;
	unsigned long			 genctr;
	int				 ret = 0;
	size_t				 offset;

	ret = connect_controller(&ctrl->ctx, ctrl->trtype,
				 ctrl->address, ctrl->port);
	if (ret)
		return ret;

	offset = offsetof(struct nvmf_disc_rsp_page_hdr, numrec);
	log_size = offset + sizeof(log->numrec);
	log_size = round_up(log_size, sizeof(u32));

	ret = send_get_log_page(&ctrl->ctx, log_size, &log);
	if (ret) {
		print_err("Failed to fetch number of discovery log entries");
		ret = -ENODATA;
		goto err;
	}

	genctr = le64toh(log->genctr);
	*numrec = le32toh(log->numrec);

	free(log);

	if (*numrec == 0) {
		print_err("No discovery log on controller %s", ctrl->address);
		ret = -ENODATA;
		goto err;
	}

	print_debug("number of records to fetch is %d", *numrec);

	log_size = sizeof(struct nvmf_disc_rsp_page_hdr) +
		sizeof(struct nvmf_disc_rsp_page_entry) * *numrec;

	ret = send_get_log_page(&ctrl->ctx, log_size, &log);
	if (ret) {
		print_err("Failed to fetch discovery log entries");
		ret = -ENODATA;
		goto err;
	}

	if ((*numrec != le32toh(log->numrec)) ||
	    ( genctr != le64toh(log->genctr))) {
		print_err("# records for last two get log pages not equal");
		ret = -EINVAL;
		goto err;
	}

	*logp = log;

err:
	disconnect_controller(&ctrl->ctx);
	return ret;
}

static void print_discovery_log(struct nvmf_disc_rsp_page_hdr *log, int numrec)
{
	int i;

	print_debug("Discovery Log Number of Records %d, "
		    "Generation counter %"PRIu64"",
		    numrec, (uint64_t)le64toh(log->genctr));

	for (i = 0; i < numrec; i++) {
		struct nvmf_disc_rsp_page_entry *e = &log->entries[i];

		print_debug("=====Discovery Log Entry %d======", i);
		print_debug("trtype:  %s", trtype_str(e->trtype));
		print_debug("adrfam:  %s", adrfam_str(e->adrfam));
		print_debug("subtype: %s", subtype_str(e->subtype));
		print_debug("treq:    %s", treq_str(e->treq));
		print_debug("portid:  %d", e->portid);
		print_debug("trsvcid: %s", e->trsvcid);
		print_debug("subnqn:  %s", e->subnqn);
		print_debug("traddr:  %s", e->traddr);

		switch (e->trtype) {
		case NVMF_TRTYPE_RDMA:
			print_debug("rdma_prtype: %s",
				    prtype_str(e->tsas.rdma.prtype));
			print_debug("rdma_qptype: %s",
				    qptype_str(e->tsas.rdma.qptype));
			print_debug("rdma_cms:    %s",
				    cms_str(e->tsas.rdma.cms));
			print_debug("rdma_pkey: 0x%04x",
				    e->tsas.rdma.pkey);
			break;
		}
	}
}

static void save_log_pages(struct nvmf_disc_rsp_page_hdr *log, int numrec,
			   struct controller *ctrl)
{
	int			i;
	struct subsystem	*subsys;
	struct nvmf_disc_rsp_page_entry *e;

	for (i = 0; i < numrec; i++) {
		e = &log->entries[i];
		subsys = ctrl->subsystem_list;
		while (subsys) {
			if ((strcmp(subsys->nqn, e->subnqn) == 0)) {
				subsys->log_page = *e;
				break;
			}
			subsys = subsys->next;
		}
		if (!subsys)
			print_err("subsystem for log page (%s) not found",
				  e->subnqn);
	}
}

static void fetch_log_pages(struct controller *ctrl)
{
	struct nvmf_disc_rsp_page_hdr	*log = NULL;
	u32				 num_records = 0;

	if (get_logpages(ctrl, &log, &num_records)) {
		print_err("Failed to get logpage for controller %s",
			  ctrl->address);
		return;
	}

	save_log_pages(log, num_records, ctrl);

	print_discovery_log(log, num_records);

	free(log);
}

static void *xport_thread(void *this_interface)
{
	struct interface		*iface;
	struct controller		*ctrl;

	iface = (struct interface *)this_interface;

	if (get_transport(iface, json_ctx)) {
		print_err("Failed to get transport for iface %d",
			  iface->interface_id);
		return NULL;
	}

	ctrl = iface->controller_list;

	while (ctrl) {
		fetch_log_pages(ctrl);
		ctrl = ctrl->next;
	}

	/* TODO: Timer refresh on log-pages */
	/* TODO: Handle RESTful request to force log-page refresh */
	/* TODO: Handle changes to JSON context */
	/* TODO: Listen and service Host requests */

	return NULL;
}

static int daemonize(void)
{
	pid_t pid, sid;

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

int init_dem(int argc, char *argv[], char **ssl_cert)
{
	int	opt;
	int	run_as_daemon = 0;

	*ssl_cert = NULL;

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto help;

	debug = 1;

	/* Process CLI options for HTTP server */
	while ((opt = getopt(argc, argv, "qdp:s:r:")) != -1) {
		switch (opt) {
		case 'q':
			debug = 0;
			break;
		case 'd':
			run_as_daemon = 1;
			break;
		case 'r':
			s_http_server_opts.document_root = optarg;
			break;
		case 'p':
			s_http_port = optarg;
			print_err("Using port %s", s_http_port);
			break;
		case 's':
			*ssl_cert = optarg;
			break;
		default:
help:
			print_err("Usage: %s %s", argv[0],
				  "{-r <root>} {-p <port>} {-s <ssl_cert>}");
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
		print_err("Error starting server on port %s: %s",
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

int init_threads(pthread_t **xport_pthread, struct interface *interfaces,
		 int count)
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
		if (pthread_create(&pthreads[i], &pthread_attr, xport_thread,
				   &(interfaces[i]))) {
			print_err("Error starting transport thread");
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

	if (getuid() != 0) {
		print_err("Must be root to run dem");
		return -1;
	}

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

	s_sig_num = 0;
	stopped = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

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

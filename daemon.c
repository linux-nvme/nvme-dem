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

#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cm.h>

#include "mongoose.h"
#include "common.h"
#include "incl/nvme.h"

#define IDLE_TIMEOUT 100
#define RETRY_COUNT  200

#define NVME_VER ((1 << 16) | (2 << 8) | 1) /* NVMe 1.2.1 */

struct mg_serve_http_opts	 s_http_server_opts;
char				*s_http_port = "22345";
void				*json_ctx;
int				 stopped;
int				 poll_timeout = 100;
int				 debug;
struct interface		*interfaces;
int				 num_interfaces;

void shutdown_dem()
{
	stopped = 1;
}

static void signal_handler(int sig_num)
{
	signal(sig_num, signal_handler);
	if (!stopped) {
		stopped = 1;
		printf("\n");
	}
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

static void *poll_loop(struct mg_mgr *mgr)
{
	while (!stopped)
		mg_mgr_poll(mgr, poll_timeout);

	mg_mgr_free(mgr);

	return NULL;
}

static int handle_property_set(struct nvme_command *cmd, int *csts)
{
	int ret = 0;

	print_debug("nvme_fabrics_type_property_set %x = %llx",
		   cmd->prop_set.offset, cmd->prop_set.value);
	if (cmd->prop_set.offset == NVME_REG_CC)
		*csts = (cmd->prop_set.value == 0x460001) ? 1 : 8;
	else
		ret = -EINVAL;

	return ret;
}

static int handle_property_get(struct nvme_command *cmd,
			       struct nvme_completion *resp, int csts)
{
	int ret = 0;

	print_debug("nvme_fabrics_type_property_get %x", cmd->prop_get.offset);
	if (cmd->prop_get.offset == NVME_REG_CSTS)
		resp->result = htole32(csts);
	else if (cmd->prop_get.offset == NVME_REG_CAP)
		resp->result64 = htole64(0x200f0003ff);
	else if (cmd->prop_get.offset == NVME_REG_VS)
		resp->result = htole32(NVME_VER);
	else
		ret = -EINVAL;

	return ret;
}

static int handle_connect(struct context *ctx, u64 length, u64 addr, u64 key,
			  void *desc)
{
	int ret;

	print_debug("nvme_fabrics_type_connect");

	ret = rma_read(ctx->ep, ctx->scq, ctx->data, length, desc, addr, key);
	if (ret) {
		print_err("rma_read returned %d", ret);
		return ret;
	}

	struct nvmf_connect_data *data = (void *) ctx->data;

	print_info("host '%s' connected", data->hostnqn);

	if (strcmp(data->subsysnqn, NVME_DISC_SUBSYS_NAME)) {
		print_err("bad subsystem '%s', expecting '%s'",
			  data->subsysnqn, NVME_DISC_SUBSYS_NAME);
		ret = -EINVAL;
	}
	if (data->cntlid != 0xffff) {
		print_err("bad controller id %x, expecting %x",
			  data->cntlid, 0xffff);
		ret = -EINVAL;
	}

	return ret;
}

static int handle_identify(struct context *ctx, struct nvme_command *cmd,
			   u64 length, u64 addr, u64 key, void *desc)
{
	struct nvme_id_ctrl *id = (void *) ctx->data;
	int ret;

	if (htole32(cmd->identify.cns) != NVME_NQN_DISC) {
		print_err("unexpected identify command");
		return -EINVAL;
	}

	memset(id, 0, sizeof(*id));

	print_debug("identify");

	memset(id->fr, ' ', sizeof(id->fr));
	strncpy((char *)id->fr, " ", sizeof(id->fr));

	id->mdts = 0;
	id->cntlid = 0;
	id->ver = htole32(NVME_VER);
	id->lpa = (1 << 2);
	id->maxcmd = htole16(NVMF_DQ_DEPTH);
	id->sgls = htole32(1 << 0) | htole32(1 << 2) | htole32(1 << 20);

	strcpy(id->subnqn, NVME_DISC_SUBSYS_NAME);

	if (length > sizeof(*id))
		length = sizeof(*id);

	ret = rma_write(ctx->ep, ctx->scq, id, length, desc, addr, key);
	if (ret)
		print_err("rma_write returned %d", ret);

	return ret;
}

static int handle_get_log_page_count(struct context *ctx,
				     struct interface *iface, u64 length,
				     u64 addr, u64 key, void *desc)
{
	struct nvmf_disc_rsp_page_hdr *log = (void *) ctx->data;
	struct controller	*ctrl;
	struct subsystem	*subsys;
	int			ret;
	int			numrec = 0;

	print_debug("get_log_page_count");

	for (ctrl = iface->controller_list; ctrl; ctrl = ctrl->next) {
		subsys = ctrl->subsystem_list;
		while (subsys) {
			if (subsys->access)
				numrec++;
			subsys = subsys->next;
		}
	}

	log->numrec = numrec;
	log->genctr = 1;

	ret = rma_write(ctx->ep, ctx->scq, log, length, desc, addr, key);
	if (ret) {
		print_err("rma_write returned %d", ret);
	}

	return ret;
}

static int handle_get_log_pages(struct context *ctx, struct interface *iface,
				u64 length, u64 addr, u64 key, void *desc)
{
	struct nvmf_disc_rsp_page_hdr *log = (void *) ctx->data;
	struct nvmf_disc_rsp_page_entry *page = (void *) &log[1];
	struct controller	*ctrl;
	struct subsystem	*subsys;
	int ret;
	int numrec = 0;

	print_debug("get_log_pages");

	for (ctrl = iface->controller_list; ctrl; ctrl = ctrl->next) {
		subsys = ctrl->subsystem_list;
		while (subsys) {
			if (subsys->access) {
				memcpy(page, &subsys->log_page, sizeof(*page));
				numrec++;
				page++;
			}
			subsys = subsys->next;
		}
	}

	log->numrec = numrec;
	log->genctr = 1;

	ret = rma_write(ctx->ep, ctx->scq, log, length, desc, addr, key);
	if (ret) {
		print_err("rma_write returned %d", ret);
	}

	return ret;
}

static void handle_request(struct interface *iface, struct context *ctx,
			   struct qe *qe, int length)
{
	struct nvme_command	*cmd;
	struct nvme_completion	*resp;
	struct nvmf_connect_command *c;
	int			ret;
	u64			len;
	void			*desc;
	u64			addr;
	u64			key;

	cmd = (struct nvme_command *) qe->buf;
	c = &cmd->connect;

	len = get_unaligned_le24(c->dptr.ksgl.length);
	key = get_unaligned_le32(c->dptr.ksgl.key);
	desc = fi_mr_desc(ctx->data_mr);
	addr = c->dptr.ksgl.addr;

	resp = (void *) ctx->cmd;
	memset(resp, 0, sizeof(*resp));

	resp->command_id = c->command_id;

#if DEBUG_REQUEST
	dump(qe->buf, length);
#else
	UNUSED(length);
#endif

	if (cmd->common.opcode == nvme_fabrics_command) {
		switch (cmd->fabrics.fctype) {
		case nvme_fabrics_type_property_set:
			ret = handle_property_set(cmd, &ctx->csts);
			break;
		case nvme_fabrics_type_property_get:
			ret = handle_property_get(cmd, resp, ctx->csts);
			break;
		case nvme_fabrics_type_connect:
			ret = handle_connect(ctx, len, addr, key, desc);
			break;
		default:
			print_err("unknown fctype %d", cmd->fabrics.fctype);
			ret = -EINVAL;
		}
	} else if (cmd->common.opcode == nvme_admin_identify)
		ret = handle_identify(ctx, cmd, len, addr, key, desc);
	else if (cmd->common.opcode == nvme_admin_get_log_page) {
		if (len == 16)
			ret = handle_get_log_page_count(ctx, iface, len, addr,
							key, desc);
		else
			ret = handle_get_log_pages(ctx, iface, len, addr, key,
						   desc);
	} else {
		print_err("unknown nvme opcode %d\n", cmd->common.opcode);
		ret = -EINVAL;
	}

	if (ret)
		resp->status = NVME_SC_DNR;

	send_msg_and_repost(ctx, qe, resp, sizeof(*resp));
}

static void *host_thread(void *this_interface)
{
	struct context		*ctx = (struct context *)this_interface;
	struct fi_cq_err_entry	 comp;
	struct fi_eq_cm_entry	 entry;
	uint32_t		 event;
	int			 retry_count = RETRY_COUNT;
	int			 ret;

	while (!stopped) {
		/* Listen and service Host requests */

		ret = fi_eq_read(ctx->eq, &event, &entry, sizeof(entry), 0);
		if (ret == sizeof(entry))
			if (event == FI_SHUTDOWN)
				goto out;

		ret = fi_cq_sread(ctx->rcq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			handle_request(ctx->iface, ctx, comp.op_context,
				       comp.len);
		else if (ret == -EAGAIN) {
			retry_count--;
			if (!retry_count)
				goto out;
		} else if (ret != -EINTR) {
			print_cq_error(ctx->rcq, ret);
			break;
		}
	}

out:
	cleanup_fabric(ctx);
	free(ctx);

	return NULL;
}

static int run_pseudo_target_for_host(struct context *ctx)
{
	struct context	*child_ctx;
	pthread_attr_t	 pthread_attr;
	pthread_t	 pthread;
	int		 ret;


	child_ctx = malloc(sizeof(*ctx));
	if (!child_ctx) {
		print_err("malloc failed");
		return -ENOMEM;
	}
	memcpy(child_ctx, ctx, sizeof(*ctx));

	ret = run_pseudo_target(child_ctx);
	if (ret) {
		print_err("unable to accept host connect");
		print_err("run_pseudo_target returned %d", ret);
		goto err;
	}

	pthread_attr_init(&pthread_attr);
	ret = pthread_create(&pthread, &pthread_attr, host_thread, child_ctx);
	if (ret) {
		print_err("failed to start host thread");
		goto err;
	}

	return 0;
err:
	free(child_ctx);
	return ret;
}

static void refresh_log_pages(struct controller *ctrl)
{
	for (; ctrl; ctrl = ctrl->next) {
		if (!ctrl->refresh)
			continue;

		ctrl->refresh_countdown--;
		if (!ctrl->refresh_countdown) {
			fetch_log_pages(ctrl);
			ctrl->refresh_countdown = ctrl->refresh / IDLE_TIMEOUT;
		}
	}
}

static void *xport_thread(void *this_interface)
{
	struct controller	*ctrl;
	struct context		 ctx = { 0 };
	int			 ret;

	ctx.iface = (struct interface *)this_interface;

	if (get_transport(ctx.iface, json_ctx)) {
		print_err("failed to get transport for iface %d",
			  ctx.iface->interface_id);
		return NULL;
	}

	for (ctrl = ctx.iface->controller_list; ctrl; ctrl = ctrl->next) {
		fetch_log_pages(ctrl);
		ctrl->refresh_countdown = ctrl->refresh / IDLE_TIMEOUT;
	}

	ret = start_pseudo_target(&ctx, ctx.iface->trtype,
				  ctx.iface->hostaddr, ctx.iface->port);
	if (ret) {
		print_err("failed to start pseudo target");
		return NULL;
	}

	while (!stopped) {
		ret = pseudo_target_check_for_host(&ctx);
		if (ret == 0)
			ret = run_pseudo_target_for_host(&ctx);
		else if (ret != -EAGAIN && ret != -EINTR)
			print_err("Host connection failed %d\n", ret);

		if (stopped)
			break;

		refresh_log_pages(ctx.iface->controller_list);

		/* TODO: Handle RESTful request to force log-page refresh */
		/* TODO: Handle changes to JSON context */
		}

	cleanup_fabric(&ctx);

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
			print_info("Using port %s", s_http_port);
			break;
		case 's':
			*ssl_cert = optarg;
			break;
		default:
help:
			print_info("Usage: %s %s", argv[0],
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
		print_err("failed to start server on port %s: %s",
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
		pthread_kill(xport_pthread[i], SIGTERM);

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

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	pthread_attr_init(&pthread_attr);

	for (i = 0; i < count; i++) {
		if (pthread_create(&pthreads[i], &pthread_attr, xport_thread,
				   &(interfaces[i]))) {
			print_err("failed to start transport thread");
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
	int			 ret = 1;

/* TODO: Do we want to restrict to root if daemenized  */
#if 0
	if (getuid() != 0) {
		print_err("must be root to run dem");
		return -1;
	}
#endif
	if (init_dem(argc, argv, &ssl_cert))
		goto out1;

	if (init_mg_mgr(&mgr, argv[0], ssl_cert))
		goto out1;

	json_ctx = init_json("config.json");
	if (!json_ctx)
		goto out1;

	num_interfaces = init_interfaces(&interfaces);
	if (num_interfaces <= 0)
		goto out2;

	stopped = 0;

	print_info("Starting server on port %s, serving '%s'",
		   s_http_port, s_http_server_opts.document_root);

	if (init_threads(&xport_pthread, interfaces, num_interfaces))
		goto out3;

	poll_loop(&mgr);

	cleanup_threads(xport_pthread, num_interfaces);

	ret = 0;
out3:
	cleanup_interfaces(interfaces);
out2:
	cleanup_json(json_ctx);
out1:
	return ret;
}

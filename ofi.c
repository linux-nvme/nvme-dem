/*
 * Copyright (C) 2017 Intel Corp., Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_rma.h>

#if 1
#define print_info(f, x...) \
	printf(f "\n", ##x)
#define print_err(f, x...) \
	printf("fi_host: %s(%d): " f "\n", __func__, __LINE__, ##x)
#else
#define print_info(f, x...) do { } while (0)
#define print_err(f, x...) do { } while (0)
#endif

#define IDLE_TIMEOUT 100
#define CTIMEOUT 100
#define AQ_DEPTH 3
#define PAGE_SIZE 4096
#define BUF_SIZE 4096

enum { DISCONNECTED, CONNECTED };

struct context {
	struct fi_info		*prov;
	struct fi_info		*info;
	struct fid_fabric	*fab;
	struct fid_domain	*dom;
	struct fid_mr		*mr;
	struct fid_pep		*pep;
	struct fid_ep		*ep;
	struct fid_eq		*eq;
	struct fid_eq		*peq;
	struct fid_cq		*rcq;
	struct fid_cq		*scq;
	int			state;
};

static int			stopped;

static void signal_handler(int sig_num)
{
	signal(sig_num, signal_handler);
	stopped = 1;
	printf("\n");
}

static void print_cq_error(struct fid_cq *cq, int n)
{
	int rc;
	struct fi_cq_err_entry entry = { 0 };

	if (n < 0)
		n = -n;

	if (n != FI_EAVAIL)
		print_err("fi_cq_sread '%s'", fi_strerror(n));

	rc = fi_cq_readerr(cq, &entry, 0);
	if (rc < 0)
		print_err("fi_cq_readerr() returns %d '%s'",
			  rc, fi_strerror(rc));
	else {
		char buf[64];

		print_err("fi_cq_readerr() prov_err '%s'(%d)",
			  fi_cq_strerror(cq, entry.prov_errno, entry.err_data,
					 buf, sizeof(buf)),
			  entry.prov_errno);
		print_err("fi_cq_readerr() err '%s'", fi_strerror(entry.err));
	}
}

static void print_eq_error(struct fid_eq *eq, int n)
{
	int rc;
	struct fi_eq_err_entry eqe = { 0 };

	if (n < 0)
		n = -n;

	if (n != FI_EAVAIL)
		print_err("fi_eq_sread '%s'", fi_strerror(n));

	rc = fi_eq_readerr(eq, &eqe, 0);
	if (rc < 0)
		print_err("fi_eq_readerr() returns %d '%s'",
			  rc, fi_strerror(rc));
	else {
		char buf[64];

		print_err("fi_eq_readerr() prov_err '%s'(%d)",
			  fi_eq_strerror(eq, eqe.prov_errno, eqe.err_data,
					 buf, sizeof(buf)),
			  eqe.prov_errno);
		print_err("fi_eq_readerr() err '%s'", fi_strerror(eqe.err));
	}
}

static int init_fabrics(struct context *ctx, uint64_t flags, char *provider,
			char *node, char *service)
{
	struct fi_info		*hints;
	int			ret;

	ctx->state		= DISCONNECTED;

	hints = fi_allocinfo();
	if (!hints) {
		print_err("no memory for hints");
		return -ENOMEM;
	}

	hints->caps			= FI_MSG;
	hints->mode			= FI_LOCAL_MR;

	hints->ep_attr->type		= FI_EP_MSG;
	hints->ep_attr->protocol	= FI_PROTO_UNSPEC;

	hints->fabric_attr->prov_name	= strdup(provider);

	ret = fi_getinfo(FI_VERSION(1, 0), node, service, flags, hints,
			 &ctx->prov);
	fi_freeinfo(hints);

	if (ret) {
		print_err("fi_getinfo() returned %d", ret);
		return ret;
	}
	if (!ctx->prov) {
		print_err("No matching provider found?");
		return -EINVAL;
	}
	ret = fi_fabric(ctx->prov->fabric_attr, &ctx->fab, NULL);
	if (ret) {
		print_err("fi_fabric returned %d", ret);
		goto free_info;
	}
	ret = fi_domain(ctx->fab, ctx->prov, &ctx->dom, NULL);
	if (ret) {
		print_err("fi_domain returned %d", ret);
		goto close_fab;
	}

	return 0;
close_fab:
	fi_close(&ctx->fab->fid);
	ctx->fab = NULL;
free_info:
	fi_freeinfo(ctx->prov);
	ctx->prov = NULL;

	return ret;
}

static void *alloc_buffer(struct context *ctx, int size)
{
	void			*buf;
	int			ret;

	if (posix_memalign(&buf, PAGE_SIZE, size)) {
		print_err("no memory for buffer, errno %d", errno);
		return NULL;
	}
	ret = fi_mr_reg(ctx->dom, buf, size, FI_RECV | FI_SEND,
			0, 0, 0, &ctx->mr, NULL);
	if (ret) {
		print_err("fi_mr_reg returned %d", ret);
		free(buf);
		return NULL;
	}

	return buf;
}

static int server_listen(struct context *ctx)
{
	struct fi_eq_attr	eq_attr = { 0 };
	struct fi_eq_cm_entry	entry;
	uint32_t		event;
	int			ret;

	ret = fi_eq_open(ctx->fab, &eq_attr, &ctx->peq, NULL);
	if (ret) {
		print_err("fi_eq_open returned %d", ret);
		return ret;
	}
	ret = fi_passive_ep(ctx->fab, ctx->prov, &ctx->pep, ctx);
	if (ret) {
		print_err("fi_passive_ep returned %d", ret);
		return ret;
	}
	ret = fi_pep_bind(ctx->pep, &ctx->peq->fid, 0);
	if (ret) {
		print_err("fi_pep_bind returned %d", ret);
		return ret;
	}
	ret = fi_listen(ctx->pep);
	if (ret) {
		print_err("fi_listen returned %d", ret);
		return ret;
	}
	printf("Waiting...\n");

	stopped = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	while (!stopped) {
		ret = fi_eq_sread(ctx->peq, &event, &entry, sizeof(entry),
				  CTIMEOUT, 0);
		if (ret == sizeof(entry))
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_eq_error(ctx->peq, ret);
		return ret;
	}
	if (stopped)
		return -ESHUTDOWN;

	if (event != FI_CONNREQ) {
		fprintf(stderr, "Unexpected CM event %d\n", event);
		return -FI_EOTHER;
	}

	ctx->info = entry.info;

	return 0;
}

static int accept_connection(struct context *ctx)
{
	struct fi_eq_cm_entry	entry;
	uint32_t		event;
	int			ret;

	ret = fi_accept(ctx->ep, NULL, 0);
	if (ret) {
		print_err("fi_accept returned %d", ret);
		return ret;
	}

	ret = fi_eq_sread(ctx->eq, &event, &entry, sizeof(entry),
			  CTIMEOUT, 0);
	if (ret != sizeof(entry)) {
		print_eq_error(ctx->eq, ret);
		return ret;
	}

	if (event != FI_CONNECTED) {
		print_err("unexpected event %d", event);
		return -FI_EOTHER;
	}

	ctx->state = CONNECTED;

	printf("Connected\n");

	return 0;
}

static int create_endpoint(struct context *ctx, struct fi_info *info)
{
	struct fi_eq_attr	eq_attr = { 0 };
	struct fi_cq_attr	cq_attr = { 0 };
	int			ret;

	info->ep_attr->tx_ctx_cnt = 1;
	info->ep_attr->rx_ctx_cnt = 2;
	info->tx_attr->iov_limit = 1;
	info->rx_attr->iov_limit = 1;
	info->tx_attr->inject_size = 0;

	cq_attr.size = 4;
	cq_attr.format = FI_CQ_FORMAT_MSG;
	cq_attr.wait_obj = FI_WAIT_UNSPEC;
	cq_attr.wait_cond = FI_CQ_COND_NONE;

	ret = fi_cq_open(ctx->dom, &cq_attr, &ctx->rcq, NULL);
	if (ret) {
		print_err("fi_cq_open for rcq returned %d", ret);
		return ret;
	}
	ret = fi_cq_open(ctx->dom, &cq_attr, &ctx->scq, NULL);
	if (ret) {
		print_err("fi_cq_open for scq returned %d", ret);
		return ret;
	}
	ret = fi_eq_open(ctx->fab, &eq_attr, &ctx->eq, NULL);
	if (ret) {
		print_err("fi_eq_open returned %d", ret);
		return ret;
	}
	ret = fi_endpoint(ctx->dom, info, &ctx->ep, ctx);
	if (ret) {
		print_err("fi_endpoint returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ctx->ep, &ctx->rcq->fid, FI_RECV);
	if (ret) {
		print_err("fi_ep_bind for rcq returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ctx->ep, &ctx->scq->fid, FI_SEND);
	if (ret) {
		print_err("fi_ep_bind for scq returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ctx->ep, &ctx->eq->fid, 0);
	if (ret) {
		print_err("fi_ep_bind for eq returned %d", ret);
		return ret;
	}
	ret = fi_enable(ctx->ep);
	if (ret) {
		print_err("fi_enable returned %d", ret);
		return ret;
	}

	return 0;
}

static int client_connect(struct context *ctx)
{
	struct fi_eq_cm_entry	entry;
	uint32_t		event;
	int			ret;

	ret = fi_connect(ctx->ep, ctx->prov->dest_addr, NULL, 0);
	if (ret) {
		print_err("fi_connect returned %d", ret);
		return ret;
	}

	printf("Waiting...\n");

	stopped = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	while (!stopped) {
		ret = fi_eq_sread(ctx->eq, &event, &entry, sizeof(entry),
				  IDLE_TIMEOUT, 0);
		if (ret == sizeof(entry))
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_eq_error(ctx->eq, ret);
		return ret;
	}

	if (event != FI_CONNECTED) {
		print_err("fi_connect failed, event %d", event);
		return -ECONNRESET;
	}

	ctx->state = CONNECTED;
	printf("Connected\n");

	return 0;
}

static void cleanup_fabric(struct context *ctx)
{
	if (ctx->state == CONNECTED) {
		fi_shutdown(ctx->ep, 0);
		ctx->state = DISCONNECTED;
	}
	if (ctx->ep)
		fi_close(&ctx->ep->fid);
	if (ctx->rcq)
		fi_close(&ctx->rcq->fid);
	if (ctx->scq)
		fi_close(&ctx->scq->fid);
	if (ctx->eq)
		fi_close(&ctx->eq->fid);
	if (ctx->info)
		fi_freeinfo(ctx->info);
	if (ctx->pep)
		fi_close(&ctx->pep->fid);
	if (ctx->peq)
		fi_close(&ctx->peq->fid);
	if (ctx->mr)
		fi_close(&ctx->mr->fid);
	if (ctx->dom)
		fi_close(&ctx->dom->fid);
	if (ctx->fab)
		fi_close(&ctx->fab->fid);
	if (ctx->prov)
		fi_freeinfo(ctx->prov);
}

int ofi_client(struct context *ctx)
{
	struct fi_cq_err_entry	comp;
	int			ret;
	char			*node = "192.168.22.1";
	char			*service = "22331";
	char			*provider = "verbs";
	char			*buf;

	ret = init_fabrics(ctx, 0, provider, node, service);
	if (ret)
		return ret;

	buf = alloc_buffer(ctx, BUF_SIZE);
	if (!buf)
		goto cleanup;

	ret = create_endpoint(ctx, ctx->prov);
	if (ret)
		goto cleanup;

	ret = fi_recv(ctx->ep, buf, BUF_SIZE, fi_mr_desc(ctx->mr), 0, NULL);
	if (ret) {
		print_err("fi_recv returned %d", ret);
		goto cleanup;
	}

	ret = client_connect(ctx);
	if (ret)
		goto cleanup;

	sprintf(buf,
		"This is a test, this is only a test\n"
		"This is a test, this is only a test\n"
		"This is a test, this is only a test\n"
		"This is a test, this is only a test\n"
		"This is a test, this is only a test\n"
		"This is a test, this is only a test\n"
		"This is a test, this is only a test\n"
		"This is a test, this is only a test\n"
		"This is a test, this is only a test\n");

	ret = fi_send(ctx->ep, buf, strlen(buf) + 1, fi_mr_desc(ctx->mr),
		      FI_ADDR_UNSPEC, NULL);
	if (ret) {
		print_err("fi_send returned %d", ret);
		goto cleanup;
	}

	while (!stopped) {
		ret = fi_cq_sread(ctx->scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_cq_error(ctx->rcq, ret);
		break;
	}

	ret = 0;

	usleep(500);

cleanup:
	cleanup_fabric(ctx);
	if (buf)
		free(buf);

	return ret;
}

int ofi_server(struct context *ctx)
{
	struct fi_cq_err_entry	comp;
	int			ret;
	char			*node = "192.168.22.1";
	char			*service = "22331";
	char			*provider = "verbs";
	char			*buf;

	ret = init_fabrics(ctx, FI_SOURCE, provider, node, service);
	if (ret)
		return ret;

	buf = alloc_buffer(ctx, BUF_SIZE);
	if (!buf)
		goto cleanup;

	ret = server_listen(ctx);
	if (ret)
		goto cleanup;

	ret = create_endpoint(ctx, ctx->info);
	if (ret)
		goto cleanup;

	ret = fi_recv(ctx->ep, buf, BUF_SIZE, fi_mr_desc(ctx->mr), 0, NULL);
	if (ret) {
		print_err("fi_recv returned %d", ret);
		goto cleanup;
	}
	ret = accept_connection(ctx);
	if (ret) 
		goto cleanup;

	while (!stopped) {
		ret = fi_cq_sread(ctx->rcq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_cq_error(ctx->rcq, ret);
		break;
	}

	if (ret > 0) {
		printf("recvd:\n%s\n", buf);
		ret = 0;
	}

cleanup:
	cleanup_fabric(ctx);
	if (buf)
		free(buf);

	return ret;
}

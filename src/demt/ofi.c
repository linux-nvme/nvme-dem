/*
 * NVMe over Fabrics Distributed Endpoint Manager (NVMe-oF DEM).
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
#include <endian.h>
#include <uuid/uuid.h>
#include <sys/socket.h>

#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_domain.h>
//#include <rdma/fi_tagged.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_rma.h>

#include "common.h"

#define CTIMEOUT	100

void dump(u8 *buf, int len)
{
	int			 i, j, n = 0;
	char			 hex[49];
	char			 prev[49];
	char			 chr[17];
	char			*p, *c;

	memset(prev, 0, sizeof(prev));
	memset(hex, 0, sizeof(hex));
	memset(chr, 0, sizeof(chr));
	c = chr;
	p = hex;

	for (i = j = 0; i < len; i++) {
		sprintf(p, "%02x ", buf[i]);
		p += 3;
		*c++ = (buf[i] >= 0x20 && buf[i] <= 0x7f) ? buf[i] : '.';

		if (++j == 16) {
			if (strcmp(hex, prev)) {
				if (n) {
					printf("----  repeated %d %s  ----\n",
					       n, n == 1 ? "time" : "times");
					n = 0;
				}
				printf("%04x  %s  %s\n", i - j + 1, hex, chr);
				strcpy(prev, hex);
			} else
				n++;
			j = 0;
			memset(hex, 0, sizeof(hex));
			memset(chr, 0, sizeof(chr));
			c = chr;
			p = hex;
		}
	}

	if (j) {
		if (strcmp(hex, prev) == 0)
			n++;
		if (n)
			printf("----  repeated %d %s  ----\n",
			       n, n == 1 ? "time" : "times");
		if (strcmp(hex, prev))
			printf("%04x  %-48s  %s\n", i - j, hex, chr);
	}
}

void print_cq_error(struct fid_cq *cq, int n)
{
	int			 rc;
	struct fi_cq_err_entry	 entry = { NULL };

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

void print_eq_error(struct fid_eq *eq, int n)
{
	int			 rc;
	struct fi_eq_err_entry	 eqe = { NULL };

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

void *alloc_buffer(struct endpoint *ep, int size, struct fid_mr **mr)
{
	void			*buf;
	int			 ret;

	if (posix_memalign(&buf, PAGE_SIZE, size)) {
		print_err("no memory for buffer, errno %d", errno);
		goto out;
	}
	memset(buf, 0, size);
	ret = fi_mr_reg(ep->dom, buf, size, FI_RECV | FI_SEND |
			FI_REMOTE_READ | FI_REMOTE_WRITE |
			FI_READ | FI_WRITE,
			0, 0, 0, mr, NULL);
	if (ret) {
		print_err("fi_mr_reg returned %d", ret);
		free(buf);
		goto out;
	}

	return buf;
out:
	*mr = NULL;
	return NULL;
}

int init_fabric(struct endpoint *ep)
{
	struct qe		*qe;
	int			 i;
	int			 ret;

	ret = fi_fabric(ep->prov->fabric_attr, &ep->fab, NULL);
	if (ret) {
		print_err("fi_fabric returned %d", ret);
		goto free_info;
	}
	ret = fi_domain(ep->fab, ep->prov, &ep->dom, NULL);
	if (ret) {
		print_err("fi_domain returned %d", ret);
		goto close_fab;
	}

	qe = calloc(sizeof(struct qe), ep->depth);
	if (!qe)
		return -ENOMEM;

	ep->qe = qe;

	for (i = 0; i < ep->depth; i++) {
		qe[i].buf = alloc_buffer(ep, BUF_SIZE, &qe[i].recv_mr);
		if (!qe[i].buf)
			return ret;
	}

	return 0;
close_fab:
	fi_close(&ep->fab->fid);
	ep->fab = NULL;
free_info:
	fi_freeinfo(ep->prov);
	ep->prov = NULL;

	return ret;
}

int init_endpoint(struct endpoint *ep, char *provider, char *node, char *srvc)
{
	struct fi_info		*hints;
	int			 ret;

	ep->state = DISCONNECTED;

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

	ret = fi_getinfo(FI_VER, node, srvc, 0, hints, &ep->prov);

	free(hints->fabric_attr->prov_name);
	hints->fabric_attr->prov_name = NULL;

	fi_freeinfo(hints);

	if (ret) {
		print_err("fi_getinfo() returned %d", ret);
		return ret;
	}
	if (!ep->prov) {
		print_err("No matching provider found?");
		return -EINVAL;
	}

	return init_fabric(ep);
}

int init_listener(struct listener *pep, char *provider, char *node, char *srvc)
{
	struct fi_info		*hints;
	int			 ret;

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

	ret = fi_getinfo(FI_VER, node, srvc, FI_SOURCE, hints, &pep->prov);

	free(hints->fabric_attr->prov_name);
	hints->fabric_attr->prov_name = NULL;

	fi_freeinfo(hints);

	if (ret) {
		print_err("fi_getinfo() returned %d", ret);
		return ret;
	}
	if (!pep->prov) {
		print_err("No matching provider found?");
		return -EINVAL;
	}
	ret = fi_fabric(pep->prov->fabric_attr, &pep->fab, NULL);
	if (ret) {
		print_err("fi_fabric returned %d", ret);
		goto free_info;
	}
	ret = fi_domain(pep->fab, pep->prov, &pep->dom, NULL);
	if (ret) {
		print_err("fi_domain returned %d", ret);
		goto close_fab;
	}

	return 0;
close_fab:
	fi_close(&pep->fab->fid);
	pep->fab = NULL;
free_info:
	fi_freeinfo(pep->prov);
	pep->prov = NULL;

	return ret;
}

int server_listen(struct listener *pep)
{
	struct fi_eq_attr	 eq_attr = { 0 };
	int			 ret;

	ret = fi_eq_open(pep->fab, &eq_attr, &pep->peq, NULL);
	if (ret) {
		print_err("fi_eq_open returned %d", ret);
		return ret;
	}
	ret = fi_passive_ep(pep->fab, pep->prov, &pep->pep, pep);
	if (ret) {
		print_err("fi_passive_ep returned %d", ret);
		return ret;
	}
	ret = fi_pep_bind(pep->pep, &pep->peq->fid, 0);
	if (ret) {
		print_err("fi_pep_bind returned %d", ret);
		return ret;
	}
	ret = fi_listen(pep->pep);
	if (ret) {
		print_err("fi_listen returned %d", ret);
		return ret;
	}

	return 0;
}

int accept_connection(struct endpoint *ep)
{
	struct fi_eq_cm_entry	 entry;
	uint32_t		 event;
	int			 ret;
	int			 i;

	for (i = 0; i < ep->depth; i++) {
		ret = fi_recv(ep->ep, ep->qe[i].buf, BUF_SIZE,
			      fi_mr_desc(ep->qe[i].recv_mr), 0, &ep->qe[i]);
		if (ret) {
			print_err("fi_recv returned %d", ret);
			return ret;
		}
	}

	ret = fi_accept(ep->ep, NULL, 0);
	if (ret) {
		print_err("fi_accept returned %d", ret);
		return ret;
	}

	ret = fi_eq_sread(ep->eq, &event, &entry, sizeof(entry),
			  CTIMEOUT, 0);
	if (ret != sizeof(entry)) {
		print_eq_error(ep->eq, ret);
		return ret;
	}

	if (event != FI_CONNECTED) {
		print_err("unexpected event %d", event);
		return -FI_EOTHER;
	}

	ep->state = CONNECTED;

	return 0;
}

int wait_for_connection(struct listener *pep, struct fi_info **info)
{
	struct fi_eq_cm_entry	 entry;
	uint32_t		 event;
	int			 ret;

	while (!stopped) {
		if (!pep->peq) {
			usleep(100);
			continue;
		}

		ret = fi_eq_sread(pep->peq, &event, &entry, sizeof(entry),
				  CTIMEOUT, 0);
		if (ret == sizeof(entry))
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			return ret;
		print_eq_error(pep->peq, ret);
		return ret;
	}

	if (stopped)
		return -ESHUTDOWN;

	if (event != FI_CONNREQ) {
		print_err("Unexpected CM event %d", event);
		return -FI_EOTHER;
	}

	*info = entry.info;

	return 0;
}

int create_endpoint(struct endpoint *ep, struct fi_info *info)
{
	struct fi_eq_attr	 eq_attr = { 0 };
	struct fi_cq_attr	 cq_attr = { 0 };
	int			 ret;

	info->ep_attr->tx_ctx_cnt = 1;
	info->ep_attr->rx_ctx_cnt = 2;
	info->tx_attr->iov_limit = 1;
	info->rx_attr->iov_limit = 1;
	info->tx_attr->inject_size = 0;

	cq_attr.size = ep->depth;
	cq_attr.format = FI_CQ_FORMAT_MSG;
	cq_attr.wait_obj = FI_WAIT_UNSPEC;
	cq_attr.wait_cond = FI_CQ_COND_NONE;

	ret = fi_cq_open(ep->dom, &cq_attr, &ep->rcq, NULL);
	if (ret) {
		print_err("fi_cq_open for rcq returned %d", ret);
		return ret;
	}
	ret = fi_cq_open(ep->dom, &cq_attr, &ep->scq, NULL);
	if (ret) {
		print_err("fi_cq_open for scq returned %d", ret);
		return ret;
	}
	ret = fi_eq_open(ep->fab, &eq_attr, &ep->eq, NULL);
	if (ret) {
		print_err("fi_eq_open returned %d", ret);
		return ret;
	}
	ret = fi_endpoint(ep->dom, info, &ep->ep, ep);
	if (ret) {
		print_err("fi_endpoint returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ep->ep, &ep->rcq->fid, FI_RECV);
	if (ret) {
		print_err("fi_ep_bind for rcq returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ep->ep, &ep->scq->fid, FI_SEND);
	if (ret) {
		print_err("fi_ep_bind for scq returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ep->ep, &ep->eq->fid, 0);
	if (ret) {
		print_err("fi_ep_bind for eq returned %d", ret);
		return ret;
	}
	ret = fi_enable(ep->ep);
	if (ret) {
		print_err("fi_enable returned %d", ret);
		return ret;
	}

	return 0;
}

int client_connect(struct endpoint *ep, void *data, int bytes)
{
	struct fi_eq_cm_entry	entry;
	uint32_t		event;
	int			i;
	int			ret;

	for (i = 0; i < ep->depth; i++) {
		if (!ep->qe[i].recv_mr)
			return -ENOMEM;

		ret = fi_recv(ep->ep, ep->qe[i].buf, BUF_SIZE,
			      fi_mr_desc(ep->qe[i].recv_mr), 0, &ep->qe[i]);
		if (ret) {
			print_err("fi_recv returned %d", ret);
			return ret;
		}
	}

	ret = fi_connect(ep->ep, ep->prov->dest_addr, data, bytes);
	if (ret) {
		print_err("fi_connect returned %d", ret);
		return ret;
	}

	while (!stopped) {
		ret = fi_eq_sread(ep->eq, &event, &entry, sizeof(entry),
				  IDLE_TIMEOUT, 0);
		if (ret == sizeof(entry))
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_eq_error(ep->eq, ret);
		return ret;
	}

	if (event != FI_CONNECTED) {
		print_err("fi_connect failed, event %d", event);
		return -ECONNRESET;
	}

	ep->state = CONNECTED;

	return 0;
}

void cleanup_endpoint(struct endpoint *ep, int shutdown)
{
	int			 i;

	if (shutdown && (ep->state != DISCONNECTED))
		fi_shutdown(ep->ep, 0);

	if (ep->qe) {
		for (i = 0; i < ep->depth; i++) {
			fi_close(&ep->qe[i].recv_mr->fid);
			free(ep->qe[i].buf);
		}
	}
	if (ep->data_mr) {
		fi_close(&ep->data_mr->fid);
		ep->data_mr = NULL;
	}
	if (ep->send_mr) {
		fi_close(&ep->send_mr->fid);
		ep->send_mr = NULL;
	}
	if (ep->ep) {
		fi_close(&ep->ep->fid);
		ep->ep = NULL;
	}
	if (ep->rcq) {
		fi_close(&ep->rcq->fid);
		ep->rcq = NULL;
	}
	if (ep->scq) {
		fi_close(&ep->scq->fid);
		ep->scq = NULL;
	}
	if (ep->eq) {
		fi_close(&ep->eq->fid);
		ep->eq = NULL;
	}
	if (ep->dom) {
		fi_close(&ep->dom->fid);
		ep->dom = NULL;
	}
	if (ep->info) {
		fi_freeinfo(ep->info);
		ep->info = NULL;
	}
	if (ep->prov) {
		fi_freeinfo(ep->prov);
		ep->prov = NULL;
	}
	if (ep->fab) {
		fi_close(&ep->fab->fid);
		ep->fab = NULL;
	}

	ep->state = DISCONNECTED;
}

void cleanup_listener(struct listener *pep)
{
	if (pep->pep) {
		fi_close(&pep->pep->fid);
		pep->pep = NULL;
	}
	if (pep->peq) {
		fi_close(&pep->peq->fid);
		pep->peq = NULL;
	}
	if (pep->dom) {
		fi_close(&pep->dom->fid);
		pep->dom = NULL;
	}
	if (pep->info) {
		fi_freeinfo(pep->info);
		pep->info = NULL;
	}
	if (pep->prov) {
		fi_freeinfo(pep->prov);
		pep->prov = NULL;
	}
	if (pep->fab) {
		fi_close(&pep->fab->fid);
		pep->fab = NULL;
	}
}

int rma_read(struct fid_ep *ep, struct fid_cq *scq, void *buf, int len,
	     void *desc, u64 addr, u64 key)
{
	struct fi_cq_err_entry	comp;
	int			ret;

	ret = fi_read(ep, buf, len, desc, (fi_addr_t) NULL, addr, key, NULL);
	if (ret)
		return ret;

	while (!stopped) {
		ret = fi_cq_sread(scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			return 0;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("fi_read failed");
		print_cq_error(scq, ret);
		break;
	}

	return ret;
}

int rma_write(struct fid_ep *ep, struct fid_cq *scq, void *buf, int len,
	      void *desc, u64 addr, u64 key)
{
	struct fi_cq_err_entry	comp;
	int			ret;

	ret = fi_write(ep, buf, len, desc, (fi_addr_t) NULL, addr, key, NULL);
	if (ret)
		return ret;

	while (!stopped) {
		ret = fi_cq_sread(scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			return 0;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("fi_write failed");
		print_cq_error(scq, ret);
		break;
	}

	return ret;
}

int send_msg_and_repost(struct endpoint *ep, struct qe *qe, void *msg, int len)
{
	struct fi_cq_err_entry comp;
	int ret;

	ret = fi_recv(ep->ep, qe->buf, BUF_SIZE, fi_mr_desc(qe->recv_mr),
		      0, qe);
	if (ret) {
		print_err("fi_recv returned %d", ret);
		return ret;
	}

	ret = fi_send(ep->ep, msg, len, fi_mr_desc(ep->send_mr),
		      FI_ADDR_UNSPEC, NULL);
	if (ret) {
		print_err("fi_send returned %d", ret);
		return ret;
	}

	while (!stopped) {
		ret = fi_cq_sread(ep->scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("fi_send failed");
		print_cq_error(ep->scq, ret);
		return ret;
	}

	return 0;
}

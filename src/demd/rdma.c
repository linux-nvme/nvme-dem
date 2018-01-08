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

#include "common.h"

#include <sys/time.h>
#include <linux/types.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "ops.h"

#define BACKLOG			16
#define RESOLVE_TIMEOUT		5000
#define EVENT_TIMEOUT		200

struct rdma_qe {
	struct ibv_mr		*recv_mr;
	void			*buf;
	__u64			 length;
};
struct rdma_ep {
	struct ibv_pd		*pd;
	struct ibv_cq		*rcq;
	struct ibv_cq		*scq;
	struct ibv_comp_channel *comp;
	struct rdma_event_channel *ec;
	struct rdma_cm_id	*id;
	struct rdma_qe		*qe;
	__u8			 state;
	__u64			 depth;
};

struct rdma_pep {
	struct ibv_mr		*mr;
	struct ibv_cq		*rcq;
	struct ibv_cq		*scq;
	struct ibv_qp		*qp;
	struct rdma_cm_id	*id;
	struct rdma_event_channel *ec;
	__u8			 state;
};

static void *alloc_buffer(struct rdma_ep *ep, int size, struct ibv_mr **_mr)
{
	void			*buf;
	struct ibv_mr		*mr;
	int			 flags = IBV_ACCESS_LOCAL_WRITE |
					 IBV_ACCESS_REMOTE_WRITE;

	if (posix_memalign(&buf, PAGE_SIZE, size)) {
		print_err("no memory for buffer, errno %d", errno);
		goto err1;
	}
	memset(buf, 0, size);

	mr = ibv_reg_mr(ep->pd, buf, size, flags);
	if (!mr)
		goto err2;

	*_mr = mr;

	return buf;
err2:
	free(buf);
err1:
	*_mr = NULL;

	return NULL;
}

static int rdma_create_queue_recv_pool(struct rdma_ep *ep)
{
	struct rdma_qe		*qe;
	struct ibv_recv_wr	 wr, *bad_wr = NULL;
	struct ibv_sge		 sge;
	u16			 i;
	int			 ret;

	qe = calloc(sizeof(struct rdma_qe), ep->depth);
	if (!qe) {
		errno = ENOMEM;
		goto err;
	}

	for (i = 0; i < ep->depth; i++) {
		qe[i].buf = alloc_buffer(ep, PAGE_SIZE, &qe[i].recv_mr);
		if (!qe[i].buf) {
			errno = ENOMEM;
			goto err;
		}
	}

	wr.next = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	sge.length = PAGE_SIZE;

	for (i = 0; i < ep->depth; i++) {
		wr.wr_id = (uintptr_t) &qe[i];
		sge.addr = (uintptr_t) qe[i].buf;
		sge.lkey = qe[i].recv_mr->lkey;

		ret = ibv_post_recv(ep->id->qp, &wr, &bad_wr);
		if (ret)
			goto err;
	}

	ep->qe = qe;

	return 0;
err:
	while (i > 0) {
		free(qe[--i].buf);
		ibv_dereg_mr(qe[i].recv_mr);
	}

	free(qe);

	return -errno;
}

static int rdma_init_endpoint(struct xp_ep **_ep, int depth)
{
	struct rdma_ep			*ep;
	struct rdma_cm_id		*id;
	struct rdma_event_channel	*ec;
	int				 flags;
	int				 ret;

	ec = rdma_create_event_channel();
	if (!ec)
		return -errno;

	flags = fcntl(ec->fd, F_GETFL);
	fcntl(ec->fd, F_SETFL, flags | O_NONBLOCK);

	ret = rdma_create_id(ec, &id, NULL, RDMA_PS_TCP);
	if (ret)
		goto err1;

	ep = malloc(sizeof(*ep));
	if (!ep) {
		ret = -ENOMEM;
		goto err2;
	}

	memset(ep, 0, sizeof(*ep));

	*_ep = (struct xp_ep *) ep;

	ep->id = id;
	ep->ec = ec;
	ep->depth = depth;

	return 0;
err2:
	rdma_destroy_id(id);
err1:
	rdma_destroy_event_channel(ec);
	return ret;
}

static int rdma_create_completion_queues(struct rdma_ep *ep)
{
	struct ibv_pd		*pd;
	struct ibv_cq		*rcq;
	struct ibv_cq		*scq;
	struct ibv_context	*ctx = ep->id->verbs;
	struct ibv_comp_channel	*comp;
	int			 flags;
	int			 ret;

	pd = ibv_alloc_pd(ctx);
	if (!pd)
		return -errno;

	comp = ibv_create_comp_channel(ctx);
	if (!comp)
		goto err1;

	flags = fcntl(comp->fd, F_GETFL);
	ret = fcntl(comp->fd, F_SETFL, flags | O_NONBLOCK);
	if (ret < 0)
		goto err1;

	rcq = ibv_create_cq(ctx, ep->depth, NULL, ep->comp, 0);
	if (!rcq)
		goto err2;

	scq = ibv_create_cq(ctx, ep->depth, NULL, ep->comp, 0);
	if (!scq)
		goto err3;

	if (ibv_req_notify_cq(rcq, 0))
		goto err4;

	if (ibv_req_notify_cq(scq, 0))
		goto err4;

	ep->pd = pd;
	ep->rcq = rcq;
	ep->scq = scq;
	ep->comp = comp;

	return 0;
err4:
	ibv_destroy_cq(scq);
err3:
	ibv_destroy_cq(rcq);
err2:
	ibv_destroy_comp_channel(comp);
err1:
	ibv_dealloc_pd(pd);

	return -errno;
}

static int rdma_create_queue_pairs(struct rdma_ep *ep)
{
	struct ibv_qp_init_attr	 qp_attr = { 0 };
	const int		 send_wr_factor = 3;	/* MR, SEND, INV */

	qp_attr.send_cq = ep->scq;
	qp_attr.recv_cq = ep->rcq;
	qp_attr.qp_type = IBV_QPT_RC;

	qp_attr.cap.max_send_wr = send_wr_factor * ep->depth + 1;
	qp_attr.cap.max_recv_wr = ep->depth + 1;
	qp_attr.cap.max_send_sge = 2;
	qp_attr.cap.max_recv_sge = 1;

	if (rdma_create_qp(ep->id, ep->pd, &qp_attr))
		return -errno;

	return 0;
}

static void _rdma_destroy_ep(struct rdma_ep *ep)
{
	int			 i = ep->depth;
	struct rdma_qe		*qe = ep->qe;

	if (qe) {
		while (i > 0) {
			if (qe[--i].buf) {
				free(qe[i].buf);
				ibv_dereg_mr(qe[i].recv_mr);
			}
		}
		free(qe);
		ep->qe = NULL;
	}
	if (ep->id) {
		rdma_destroy_qp(ep->id);
		ep->id = NULL;
	}
	if (ep->rcq) {
		ibv_destroy_cq(ep->rcq);
		ep->rcq = NULL;
	}
	if (ep->scq) {
		ibv_destroy_cq(ep->scq);
		ep->scq = NULL;
	}
	if (ep->ec) {
		rdma_destroy_event_channel(ep->ec);
		ep->ec = NULL;
	}
	if (ep->comp) {
		ibv_destroy_comp_channel(ep->comp);
		ep->comp = NULL;
	}
	if (ep->pd) {
		ibv_dealloc_pd(ep->pd);
		ep->pd = NULL;
	}
}

static void rdma_destroy_endpoint(struct xp_ep *_ep)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;

	_rdma_destroy_ep(ep);
}

static int _rdma_create_ep(struct rdma_ep *ep)
{
	if (rdma_create_completion_queues(ep))
		goto err;

	if (rdma_create_queue_pairs(ep))
		goto err;

	if (rdma_create_queue_recv_pool(ep))
		goto err;

	return 0;
err:
	_rdma_destroy_ep(ep);

	return -errno;
}

static int rdma_create_endpoint(struct xp_ep **_ep, void *id, int depth)
{
	struct rdma_ep		*ep;
	int			ret;

	ep = malloc(sizeof(*ep));
	if (!ep)
		return -ENOMEM;

	memset(ep, 0, sizeof(*ep));

	ep->id = id;
	ep->depth = depth;

	ret = _rdma_create_ep(ep);
	if (ret)
		return ret;

	*_ep = (struct xp_ep *) ep;

	return 0;
}

static int rdma_init_listener(struct xp_pep **_pep, char *srvc)
{
	struct rdma_pep			*pep;
	struct sockaddr_in		 addr = { 0 };
	struct rdma_cm_id		*listener;
	struct rdma_event_channel	*ec;
	int				 flags;
	int				 ret;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(srvc));

	ec = rdma_create_event_channel();
	if (!ec)
		return -errno;

	flags = fcntl(ec->fd, F_GETFL);
	fcntl(ec->fd, F_SETFL, flags | O_NONBLOCK);

	ret = rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP);
	if (ret)
		goto err1;

	ret = rdma_bind_addr(listener, (struct sockaddr *) &addr);
	if (ret)
		goto err2;

	ret = rdma_listen(listener, BACKLOG);
	if (ret)
		goto err2;

	pep = malloc(sizeof(*pep));
	if (!pep) {
		ret = -ENOMEM;
		goto err2;
	}

	memset(pep, 0, sizeof(*pep));

	*_pep = (struct xp_pep *) pep;

	pep->id = listener;
	pep->ec = ec;

	return 0;

err2:
	rdma_destroy_id(listener);
err1:
	rdma_destroy_event_channel(ec);
	return ret;
}

static int rdma_accept_connection(struct xp_ep *_ep)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct rdma_conn_param	 params = { 0 };

	params.initiator_depth	= RDMA_MAX_INIT_DEPTH;
	params.responder_resources = RDMA_MAX_RESP_RES;
	params.flow_control	= 1;
	params.retry_count	= 15;
	params.rnr_retry_count	= 7;

	return rdma_accept(ep->id, &params);
}

static int rdma_reject_connection(struct xp_ep *_ep, void *data, int len)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;

	return rdma_reject(ep->id, data, len);
}

static int rdma_wait_for_event(struct rdma_event_channel *ec,
			       struct rdma_cm_event **event)
{
	struct timeval		 tv;
	fd_set			 fds, rfds;
	int			 ret = 0;

	FD_ZERO(&fds);
	FD_SET(ec->fd, &fds);
	tv.tv_sec = 0;

	while (!ret) {
		tv.tv_usec = EVENT_TIMEOUT;
		rfds = fds;
		ret = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
		if (ret < 0)
			return ret;
		if (stopped)
			return -ESHUTDOWN;
	}

	while (rdma_get_cm_event(ec, event)) {
		if (errno != EAGAIN)
			return -errno;
		if (stopped)
			return -ESHUTDOWN;
	}

	return 0;
}

static int rdma_wait_for_connection(struct xp_pep *_pep, void **_id)
{
	struct rdma_pep		*pep = (struct rdma_pep *) _pep;
	struct rdma_cm_event	*event;
	struct rdma_cm_id	*id;
	enum rdma_cm_event_type	 ev;

	if (rdma_wait_for_event(pep->ec, &event))
		return -ENOTCONN;

	id = event->id;
	ev = event->event;

	rdma_ack_cm_event(event);

	if (ev == RDMA_CM_EVENT_ESTABLISHED)
		return -EISCONN;
	if (ev == RDMA_CM_EVENT_DISCONNECTED)
		return -ECONNRESET;
	if (ev != RDMA_CM_EVENT_CONNECT_REQUEST)
		return -ENOTCONN;

	*_id = id;

	return 0;
}

static void route_resolved(struct rdma_ep *ep, struct rdma_cm_id *id,
			   void *data, int bytes)
{
	struct rdma_conn_param	 params = { 0 };
	int			 ret;

	params.initiator_depth	= RDMA_MAX_INIT_DEPTH;
	params.responder_resources = 1;
	params.flow_control	= 1;
	params.retry_count	= 15;
	params.rnr_retry_count	= 7;

	params.private_data	= data;
	params.private_data_len	= bytes;

	ret = _rdma_create_ep(ep);
	if (ret)
		print_err("_rdma_create_ep failed %d", ret);
	else if (rdma_connect(id, &params))
		print_err("rdma_connect failed %d", errno);
}

static void addr_resolved(struct rdma_cm_id *id)
{
	int			 ret;

	ret = rdma_resolve_route(id, RESOLVE_TIMEOUT);
	if (ret)
		print_err("rdma_resolve_route failed %d", errno);
}

static int rdma_client_connect(struct xp_ep *_ep, struct sockaddr *dst,
			       void *data, int len)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct rdma_cm_event	*event = NULL;
	enum rdma_cm_event_type	 ev;

	if (rdma_resolve_addr(ep->id, NULL, dst, RESOLVE_TIMEOUT))
		return -EADDRNOTAVAIL;

	while (!stopped) {
		if (rdma_wait_for_event(ep->ec, &event))
			break;

		if (ep->id != event->id) {
			print_err("event not from endpoint ep %p ev %p",
				  ep->id, event->id);
			break;
		}

		ev = event->event;

		rdma_ack_cm_event(event);
		switch (ev) {
		case RDMA_CM_EVENT_ADDR_RESOLVED:
			addr_resolved(ep->id);
			continue;
		case RDMA_CM_EVENT_ROUTE_RESOLVED:
			route_resolved(ep, ep->id, data, len);
			continue;
		case RDMA_CM_EVENT_ESTABLISHED:
			if (rdma_create_queue_recv_pool(ep))
				return -errno;
			ep->state = CONNECTED;
			return 0;
		default:
			break;
		}

		break;
	}

	if (!stopped)
		return -ENOTCONN;

	return -ESHUTDOWN;
}

static void rdma_destroy_listener(struct xp_pep *_pep)
{
	struct rdma_pep		*pep = (struct rdma_pep *) _pep;

	rdma_destroy_id(pep->id);
	rdma_destroy_event_channel(pep->ec);
}

static int rdma_rma_read(struct xp_ep *_ep, void *buf, u64 addr, u64 len,
			 u32 rkey, struct xp_mr *_mr)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct ibv_mr		*mr = (struct ibv_mr *) _mr;
	struct ibv_send_wr	 wr, *bad_wr = NULL;
	struct ibv_sge		 sge;
	struct ibv_wc		 wc;
	int			 ret;

	memset(&wr, 0, sizeof(wr));

	wr.sg_list	= &sge;
	wr.num_sge	= 1;

	sge.length	= len;
	sge.addr	= (uintptr_t) buf;
	sge.lkey	= mr->lkey;

	wr.opcode		= IBV_WR_RDMA_READ;
	wr.wr.rdma.remote_addr	= (uintptr_t) addr;
	wr.wr.rdma.rkey		= rkey;
	wr.send_flags		= IBV_SEND_SIGNALED;

	ret = ibv_post_send(ep->id->qp, &wr, &bad_wr);
	if (ret)
		return ret;

	while (ibv_poll_cq(ep->scq, 1, &wc) == 0)
		if (stopped)
			return -ESHUTDOWN;

	if (wc.status != IBV_WC_SUCCESS) {
		print_err("rma_read wc.status %d", wc.status);
		return -ECONNRESET;
	}

	return 0;
}

static int rdma_rma_write(struct xp_ep *_ep, void *buf, u64 addr, u64 len,
			  u32 rkey, struct xp_mr *_mr)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct ibv_mr		*mr = (struct ibv_mr *) _mr;
	struct ibv_send_wr	 wr, *bad_wr = NULL;
	struct ibv_sge		 sge;
	struct ibv_wc		 wc;
	int			 ret;

	memset(&wr, 0, sizeof(wr));

	wr.sg_list	= &sge;
	wr.num_sge	= 1;

	sge.length	= len;
	sge.addr	= (uintptr_t) buf;
	sge.lkey	= mr->lkey;

	wr.opcode		= IBV_WR_RDMA_WRITE;
	wr.wr.rdma.remote_addr	= (uintptr_t) addr;
	wr.wr.rdma.rkey		= rkey;
	wr.send_flags		= IBV_SEND_SIGNALED;

	ret = ibv_post_send(ep->id->qp, &wr, &bad_wr);
	if (ret)
		return ret;

	while (ibv_poll_cq(ep->scq, 1, &wc) == 0)
		if (stopped)
			return -ESHUTDOWN;

	if (wc.status != IBV_WC_SUCCESS) {
		print_err("rma_write wc.status %d", wc.status);
		return -ECONNRESET;
	}

	return 0;
}

static int rdma_repost_recv(struct xp_ep *_ep, struct xp_qe *_qe)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct rdma_qe		*qe = (struct rdma_qe *) _qe;
	struct ibv_recv_wr	 wr, *bad_wr = NULL;
	struct ibv_sge		 sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id	= (uintptr_t) qe;
	wr.sg_list	= &sge;
	wr.num_sge	= 1;

	sge.length	= PAGE_SIZE;
	sge.addr	= (uintptr_t) qe->buf;
	sge.lkey	= qe->recv_mr->lkey;

	memset(qe->buf, 0, PAGE_SIZE);

	return ibv_post_recv(ep->id->qp, &wr, &bad_wr);
}

static int rdma_post_msg(struct xp_ep *_ep, void *msg, int len,
			 struct xp_mr *_mr)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct ibv_mr		*mr = (struct ibv_mr *) _mr;
	struct ibv_send_wr	 wr, *bad_wr = NULL;
	struct ibv_sge		 sge;

	memset(&wr, 0, sizeof(wr));

	wr.opcode	= IBV_WR_SEND;
	wr.send_flags	= IBV_SEND_SIGNALED;
	wr.sg_list	= &sge;
	wr.num_sge	= 1;

	sge.length	= len;
	sge.addr	= (uintptr_t) msg;
	sge.lkey	= mr->lkey;

	return ibv_post_send(ep->id->qp, &wr, &bad_wr);
}

static int rdma_send_msg(struct xp_ep *_ep, void *msg, int len,
			 struct xp_mr *_mr)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct ibv_wc		 wc;
	int			 ret;

	ret = rdma_post_msg(_ep, msg, len, _mr);
	if (ret)
		return ret;

	while (ibv_poll_cq(ep->scq, 1, &wc) == 0)
		if (stopped)
			return -ESHUTDOWN;

	if (wc.status != IBV_WC_SUCCESS) {
		print_err("send wc.status %d", wc.status);
		return -ECONNRESET;
	}

	return 0;
}

static int rdma_poll_for_msg(struct xp_ep *_ep, struct xp_qe **_qe, void **msg,
			     int *bytes)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct rdma_qe		*qe;
	struct ibv_wc		 wc;
	int			 ret;

	ret = ibv_poll_cq(ep->rcq, 1, &wc);
	if (ret < 0)
		return ret;
	if (!ret)
		return -EAGAIN;

	if (wc.status != IBV_WC_SUCCESS) {
		print_err("recv wc.status %d", wc.status);
		return -ECONNRESET;
	}

	qe = (struct rdma_qe *) wc.wr_id;

	*_qe = (struct xp_qe *) qe;

	*msg = qe->buf;
	*bytes = wc.byte_len;

	return 0;
}

static int rdma_alloc_key(struct xp_ep *_ep, void *buf, int len,
			  struct xp_mr **_mr)
{
	struct rdma_ep		*ep = (struct rdma_ep *) _ep;
	struct ibv_mr		*mr;
	int			 flags = IBV_ACCESS_LOCAL_WRITE
					| IBV_ACCESS_REMOTE_READ
					| IBV_ACCESS_REMOTE_WRITE;

	if (!_ep || !_mr) {
		print_err("invalid arguments");
		return -EINVAL;
	}

	mr = ibv_reg_mr(ep->pd, buf, len, flags);
	if (!mr) {
		print_err("ibv_reg_mr failed %d", errno);
		return -errno;
	}

	*_mr = (struct xp_mr *) mr;

	return 0;
}

static u32 rdma_remote_key(struct xp_mr *_mr)
{
	struct ibv_mr		*mr = (struct ibv_mr *) _mr;

	if (!_mr) {
		print_err("invalid arguments");
		return 0;
	}

	return mr->rkey;
}

static int rdma_dealloc_key(struct xp_mr *_mr)
{
	struct ibv_mr		*mr = (struct ibv_mr *) _mr;

	if (!_mr) {
		print_err("invalid arguments");
		return -EINVAL;
	}

	return ibv_dereg_mr(mr);
}

struct xp_ops rdma_ops = {
	.init_endpoint		= rdma_init_endpoint,
	.create_endpoint	= rdma_create_endpoint,
	.destroy_endpoint	= rdma_destroy_endpoint,
	.init_listener		= rdma_init_listener,
	.destroy_listener	= rdma_destroy_listener,
	.wait_for_connection	= rdma_wait_for_connection,
	.accept_connection	= rdma_accept_connection,
	.reject_connection	= rdma_reject_connection,
	.client_connect		= rdma_client_connect,
	.rma_read		= rdma_rma_read,
	.rma_write		= rdma_rma_write,
	.repost_recv		= rdma_repost_recv,
	.post_msg		= rdma_post_msg,
	.send_msg		= rdma_send_msg,
	.poll_for_msg		= rdma_poll_for_msg,
	.alloc_key		= rdma_alloc_key,
	.remote_key		= rdma_remote_key,
	.dealloc_key		= rdma_dealloc_key,
};

struct xp_ops *rdma_register_ops(void)
{
	return &rdma_ops;
}

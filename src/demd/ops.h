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

#ifndef __OPS_H__
#define __OPS_H__

#include <sys/socket.h>

struct xp_ep;
struct xp_pep;
struct xp_qe;
struct xp_mr;

struct xp_ops {
	int (*init_endpoint)(struct xp_ep **ep, int depth);
	int (*create_endpoint)(struct xp_ep **ep, void *id, int depth);
	void (*destroy_endpoint)(struct xp_ep *ep);
	int (*init_listener)(struct xp_pep **pep, char *srvc);
	void (*destroy_listener)(struct xp_pep *pep);
	int (*wait_for_connection)(struct xp_pep *pep, void **id);
	int (*accept_connection)(struct xp_ep *ep);
	int (*reject_connection)(struct xp_ep *ep, void *data, int len);
	int (*client_connect)(struct xp_ep *ep, struct sockaddr *dst,
			      void *data, int len);
	int (*rma_read)(struct xp_ep *ep, void *buf, u64 addr, u64 len,
			u32 key, struct xp_mr *mr);
	int (*rma_write)(struct xp_ep *ep, void *buf, u64 addr, u64 len,
			 u32 key, struct xp_mr *mr);
	int (*repost_recv)(struct xp_ep *ep, struct xp_qe *qe);
	int (*post_msg)(struct xp_ep *ep, void *msg, int len,
			struct xp_mr *mr);
	int (*send_msg)(struct xp_ep *ep, void *msg, int len,
			struct xp_mr *mr);
	int (*wait_for_msg)(struct xp_ep *ep, struct xp_qe **qe, void **msg,
			    int *bytes);
	int (*alloc_key)(struct xp_ep *ep, void *buf, int len,
			 struct xp_mr **mr);
	u32 (*remote_key)(struct xp_mr *mr);
	int (*dealloc_key)(struct xp_mr *mr);
};

struct xp_ops *rdma_register_ops(void);

#endif /* __OPS_H__ */

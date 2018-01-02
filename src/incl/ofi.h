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

#ifndef __OFI_H__
#define __OFI_H__

#include <rdma/fabric.h>

#define FI_VER		FI_VERSION(1, 0)

#define MAX_NQN_SIZE	256
#define CTIMEOUT	100
#define BUF_SIZE	4096
#define PAGE_SIZE	4096

#define  u8  __u8
#define  u16 __u16
#define  u32 __u32
#define  u64 __u64

enum { DISCONNECTED, CONNECTED };

struct qe {
	struct fid_mr		*recv_mr;
	u8			*buf;
};

struct endpoint {
	char			 nqn[MAX_NQN_SIZE + 1];
	struct fi_info		*prov;
	struct fi_info		*info;
	struct fid_fabric	*fab;
	struct fid_domain	*dom;
	struct fid_ep		*ep;
	struct fid_eq		*eq;
	struct fid_cq		*rcq;
	struct fid_cq		*scq;
	struct fid_mr		*send_mr;
	struct fid_mr		*data_mr;
	struct nvme_command	*cmd;
	void			*data;
	struct qe		*qe;
	int			depth;
	int			state;
	int			csts;
};

struct listener {
	struct fi_info		*prov;
	struct fi_info		*info;
	struct fid_fabric	*fab;
	struct fid_domain	*dom;
	struct fid_pep		*pep;
	struct fid_eq		*peq;
};

#endif

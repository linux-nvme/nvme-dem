/* SPDX-License-Identifier: DUAL GPL-2.0/BSD */
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

#ifndef __COMMON_H__
#define __COMMON_H__

#define unlikely __glibc_unlikely

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

#include "nvme.h"
#include "utils.h"
#include "ops.h"
#include "dem.h"

#define DELAY			480 /* ms */
#define SECONDS			(1000/DELAY)
#define KEEP_ALIVE_COUNTER	4 /* x DELAY */
#define CONNECT_RETRY_COUNTER	(60 * SECONDS)

// TODO customize discovery controller info
#define DEFAULT_TYPE		"rdma"
#define DEFAULT_FAMILY		"ipv4"
#define DEFAULT_ADDR		"192.168.22.2"
#define DEFAULT_PORT		"4422"

#define NVME_FABRICS_DEV	"/dev/nvme-fabrics"
#define SYS_CLASS_PATH		"/sys/class/nvme-fabrics/ctl/"
#define SYS_CLASS_ADDR_FILE	"address"
#define SYS_CLASS_TRTYPE_FILE	"transport"
#define SYS_CLASS_SUBNQN_FILE	"subsysnqn"
#define NVME_FABRICS_FMT	\
	"transport=%s,traddr=%s,trsvcid=%s,nqn=%s,hostnqn=%s"

struct target;

struct portid {
	struct list_head	 node;
	int			 portid;
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	char			 port[CONFIG_PORT_SIZE + 1];
	int			 port_num;
	int			 addr[ADDR_LEN];
	int			 adrfam;
	int			 trtype;
	int			 valid;
};

struct host_iface {
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	int			 addr[ADDR_LEN];
	char			 port[CONFIG_PORT_SIZE + 1];
	struct xp_pep		*listener;
	struct xp_ops		*ops;
};

struct inb_iface {
	struct target		*target;
	struct portid		 portid;
	struct endpoint		 ep;
	int			 connected;
};

struct oob_iface {
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	int			 port;
};

union sc_iface {
	struct oob_iface	 oob;
	struct inb_iface	 inb;
};

struct fabric_iface {
	struct list_head	 node;
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 fam[CONFIG_FAMILY_SIZE + 1];
	char			 addr[CONFIG_ADDRESS_SIZE + 1];
	int			 valid;
};

struct logpage {
	struct list_head	 node;
	struct portid		*portid;
	struct nvmf_disc_rsp_page_entry e;
	int			 valid;
	int			 connected;
};

struct subsystem {
	struct list_head	 node;
	struct list_head	 logpage_list;
	struct target		*target;
	char			 nqn[MAX_NQN_SIZE + 1];
};

struct target {
	struct list_head	 node;
	struct list_head	 subsys_list;
	char			 alias[MAX_ALIAS_SIZE + 1];
	int			 mgmt_mode;
	union sc_iface		 sc_iface;
};

int init_interfaces(void);
void *interface_thread(void *arg);

void build_target_list(void);
void init_targets(void);
void cleanup_targets(void);
void get_host_nqn(void *context, void *haddr, char *nqn);

#endif

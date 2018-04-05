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
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

/* NOTE: Using linux kernel include here */
#include "linux/list.h"
#include "linux/nvme.h"
#include "linux/kernel.h"

#include "dem.h"

extern int			 debug;
extern struct list_head		*devices;
extern struct list_head		*interfaces;

#define IPV4_LEN		4
#define IPV4_OFFSET		4
#define IPV4_DELIM		"."

#define IPV6_LEN		8
#define IPV6_OFFSET		8
#define IPV6_DELIM		":"

#define FC_LEN			8
#define FC_OFFSET		4
#define FC_DELIM		":"

struct host {
	struct list_head	 node;
	struct subsystem	*subsystem;
	char			 nqn[MAX_NQN_SIZE + 1];
};

struct nsdev {
	struct list_head	 node;
	int			 devid;
	int			 nsid;
};

struct oob_iface {
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
};

struct inb_iface {
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	int			 addr[ADDR_LEN];
	char			 port[CONFIG_PORT_SIZE + 1];
	struct endpoint		 ep;
	struct xp_pep		*listener;
	struct xp_ops		*ops;
	int			 connected;
};

struct host_iface {
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	int			 addr[ADDR_LEN];
	char			 port[CONFIG_PORT_SIZE + 1];
	int			 port_num;
	int			 adrfam;
	struct endpoint		 ep;
	struct xp_pep		*listener;
	struct xp_ops		*ops;
};

struct interface {
	struct list_head	 node;
	int			 is_oob;
	union {
		struct oob_iface oob;
		struct inb_iface inb;
	} u;
};

struct portid {
	struct list_head	 node;
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	int			 addr[ADDR_LEN];
	char			 port[CONFIG_PORT_SIZE + 1];
	int			 port_num;
	int			 treq;
};

struct subsystem {
	struct list_head	 node;
	struct list_head	 host_list;
	struct list_head	 portid_list;
	struct list_head	 ns_list;
	char			 nqn[MAX_NQN_SIZE + 1];
	int			 allowany;
};

struct target {
	struct host_iface	*iface;
	char			 alias[MAX_ALIAS_SIZE + 1];
	int			 mgmt_mode;
	int			 refresh;
	int			 log_page_retry_count;
	int			 refresh_countdown;
	int			 kato_countdown;
};

struct mg_connection;

void shutdown_dem(void);
void handle_http_request(struct mg_connection *c, void *ev_data);

void *interface_thread(void *arg);
int start_pseudo_target(struct host_iface *iface);
int run_pseudo_target(struct endpoint *ep, void *id);

void reset_config(void);
int create_subsys(char *subsys, int allowany);
int delete_subsys(char *subsys);
int create_ns(char *subsys, int nsid, int devid, int devnsid);
int delete_ns(char *subsys, int nsid);
int create_host(char *host);
int delete_host(char *host);
int create_portid(int portid, char *fam, char *typ, int req, char *addr,
		  int svcid);
int delete_portid(int portid);
int link_host_to_subsys(char *subsys, char *host);
int unlink_host_from_subsys(char *subsys, char *host);
int link_port_to_subsys(char *subsys, int portid);
int unlink_port_from_subsys(char *subsys, int portid);
int enumerate_devices(void);
int enumerate_interfaces(void);
void free_devices(void);
void free_interfaces(void);

#endif

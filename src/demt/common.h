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

#include "linux/list.h"
#include "linux/nvme.h"	/* NOTE: Using linux kernel include here */

#include "ofi.h"

#define MINUTES		(60 * 1000) /* convert ms to minutes */

#define IDLE_TIMEOUT	100

#define print_debug(f, x...) \
	do { \
		if (debug) { \
			printf("%s(%d) " f "\n", __func__, __LINE__, ##x); \
			fflush(stdout); \
		} \
	} while (0)
#define print_info(f, x...)\
	do { \
		printf(f "\n", ##x); \
		fflush(stdout); \
	} while (0)
#define print_err(f, x...)\
	do { \
		fprintf(stderr, "%s(%d) Error: " f "\n", \
			__func__, __LINE__, ##x); \
		fflush(stderr); \
	} while (0)

#define UNUSED(x) ((void) x)

#define min(x, y) ((x < y) ? x : y)

#define __round_mask(x, y) ((__typeof__(x))((y) - 1))
#define round_up(x, y) ((((x) - 1) | __round_mask(x, y)) + 1)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

extern int			 debug;
extern int			 stopped;
extern struct list_head		*devices;
extern struct list_head		*interfaces;

/*
 *  trtypes
 *	[NVMF_TRTYPE_RDMA]	= "rdma",
 *	[NVMF_TRTYPE_TCPIP]	= "tcp"
 *	[NVMF_TRTYPE_FC]	= "fc",
 *	[NVMF_TRTYPE_LOOP]	= "loop",
 *
 *  adrfam
 *	[NVMF_ADDR_FAMILY_IP4]	= "ipv4",
 *	[NVMF_ADDR_FAMILY_IP6]	= "ipv6",
 *	[NVMF_ADDR_FAMILY_IB]	= "ib",
 *	[NVMF_ADDR_FAMILY_FC]	= "fc",
 */

#define CONFIG_TYPE_SIZE	8
#define CONFIG_FAMILY_SIZE	8
#define CONFIG_ADDRESS_SIZE	40
#define CONFIG_PORT_SIZE	8
#define CONFIG_DEVICE_SIZE	20
#define LARGEST_TAG		8
#define LARGEST_VAL		40
#define ADDR_LEN		16 /* IPV6 is current longest address */

#define MAX_ALIAS_SIZE		64

#ifndef AF_IPV4
#define AF_IPV4			1
#define AF_IPV6			2
#endif

#ifndef AF_FC
#define AF_FC			3
#endif


#define IPV4_LEN		4
#define IPV4_OFFSET		4
#define IPV4_DELIM		"."

#define IPV6_LEN		8
#define IPV6_OFFSET		8
#define IPV6_DELIM		":"

#define FC_LEN			8
#define FC_OFFSET		4
#define FC_DELIM		":"

//TODO remove once tags.h is updated
#define TAG_NSID		"nsid"
#define TAG_DEVID		"devid"
#define TAG_DEVNSID		"devnsid"
#define NULLB_DEVID		-1

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

struct port_id {
	struct list_head	 node;
	struct fi_info		*prov;
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
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

struct mg_connection;

void shutdown_dem(void);
void handle_http_request(struct mg_connection *c, void *ev_data);
void dump(u8 *buf, int len);

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

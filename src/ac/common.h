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

/* NOTE: Using linux kernel include here */
#include "linux/list.h"
#include "linux/nvme.h"
#include "linux/kernel.h"

#include "ops.h"

#define NVMF_UUID_FMT   "nqn.2014-08.org.nvmexpress:NVMf:uuid:%s"

#define PAGE_SIZE	4096
#define BUF_SIZE	4096
#define NVMF_DQ_DEPTH	1
#define IDLE_TIMEOUT	100
#define MINUTES		(60 * 1000) /* convert ms to minutes */
#define LOG_PAGE_RETRY	200

#define FI_VER		FI_VERSION(1, 0)

#define print_debug(f, x...) \
	do { \
		if (debug) { \
			printf("%s(%d) " f "\n", __func__, __LINE__, ##x); \
			fflush(stdout); \
		} \
	} while (0)
#define print_trace()\
	do { \
		printf("%s(%d)\n", __func__, __LINE__); \
		fflush(stdout); \
	} while (0)
#define print_info(f, x...)\
	do { \
		printf(f "\n", ##x); \
		fflush(stdout); \
	} while (0)
#define print_err(f, x...)\
	do { \
		fprintf(stderr, "Error: " f "\n", ##x); \
		fflush(stderr); \
	} while (0)

#define UNUSED(x) ((void) x)

#define min(x, y) ((x < y) ? x : y)

#define __round_mask(x, y) ((__typeof__(x))((y) - 1))
#define round_up(x, y) ((((x) - 1) | __round_mask(x, y)) + 1)

extern int			 debug;
extern int			 stopped;
extern int			 num_interfaces;
extern struct host_iface	*interfaces;
extern struct list_head		*target_list;

enum { DISCONNECTED, CONNECTED };

static inline u32 get_unaligned_le24(const u8 *p)
{
	return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16;
}

static inline u32 get_unaligned_le32(const u8 *p)
{
	return (u32) p[0] | (u32) p[1] << 8 |
		(u32) p[2] << 16 | (u32) p[3] << 24;
}

static inline int msec_delta(struct timeval t0)
{
	struct timeval		t1;

	gettimeofday(&t1, NULL);

	return (t1.tv_sec - t0.tv_sec) * 1000 +
		(t1.tv_usec - t0.tv_usec) / 1000;
}

/*HACK*/
#define NUM_CONFIG_ITEMS	3
#define CONFIG_TYPE_SIZE	8
#define CONFIG_FAMILY_SIZE	8
#define CONFIG_ADDRESS_SIZE	40
#define CONFIG_PORT_SIZE	8
#define CONFIG_DEVICE_SIZE	256
#define ADDR_LEN		16 /* IPV6 is current longest address */

#define MAX_NQN_SIZE		256
#define MAX_ALIAS_SIZE		64

#ifndef AF_IPV4
#define AF_IPV4			1
#define AF_IPV6			2
#endif

#ifndef AF_FC
#define AF_FC			3
#endif

/* HACK - Figure out which of these we need */
#define DISC_BUF_SIZE		4096
#define PATH_NVME_FABRICS	"/dev/nvme-fabrics"
#define PATH_NVMF_DEM_DISC	"/etc/nvme/nvmeof-dem/"
#define PATH_NVMF_DISC		"/etc/nvme/discovery.conf"
#define PATH_NVMF_HOSTNQN	"/etc/nvme/hostnqn"
#define SYS_NVME		"/sys/class/nvme"

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
	char			 pseudo_target_port[CONFIG_PORT_SIZE + 1];
	struct xp_pep		*listener;
	struct xp_ops		*ops;
};

struct qe {
	struct xp_qe		*qe;
	u8			*buf;
};

struct endpoint {
	char			 nqn[MAX_NQN_SIZE + 1];
	struct xp_ep		*ep;
	struct xp_mr		*mr;
	struct xp_mr		*data_mr;
	struct xp_ops		*ops;
	struct nvme_command	*cmd;
	struct qe		*qe;
	void			*data;
	int			 depth;
	int			 state;
	int			 csts;
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

struct discovery_queue {
	struct list_head	 node;
	struct portid		*portid;
	struct target		*target;
	struct endpoint		 ep;
	int			 connected;
	char			 hostnqn[MAX_NQN_SIZE + 1];
};

struct target {
	struct list_head	 node;
	struct list_head	 discovery_queue_list;
	struct list_head	 fabric_iface_list;
	char			 alias[MAX_ALIAS_SIZE + 1];
	int			 mgmt_mode;
	union sc_iface		 sc_iface;
	int			 refresh;
	int			 log_page_retry_count;
	int			 refresh_countdown;
	int			 kato_countdown;
};

enum { LOCAL_MGMT = 0, IN_BAND_MGMT, OUT_OF_BAND_MGMT };

void shutdown_dem(void);

int parse_line(FILE *fd, char *tag, int tag_max, char *value, int value_max);

int ipv4_to_addr(char *p, int *addr);
int ipv6_to_addr(char *p, int *addr);
int fc_to_addr(char *p, int *addr);

int init_interfaces(void);
void *interface_thread(void *arg);

void build_target_list(void);
void init_targets(void);
void cleanup_targets(void);
void get_host_nqn(void *context, void *haddr, char *nqn);
int connect_target(struct discovery_queue *dq);
void disconnect_target(struct endpoint *ep, int shutdown);
int client_connect(struct endpoint *ep, void *data, int bytes);

int send_get_log_page(struct endpoint *ep, int log_size,
		      struct nvmf_disc_rsp_page_hdr **log);
int send_keep_alive(struct endpoint *ep);

void fetch_log_pages(struct discovery_queue *dq);

void dump(u8 *buf, int len);

#endif

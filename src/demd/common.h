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
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

/* NOTE: Using linux kernel include here */
#include "linux/list.h"
#include "linux/nvme.h"
#include "linux/kernel.h"

#define PAGE_SIZE	4096
#define BUF_SIZE	4096
#define NVMF_DQ_DEPTH	1
#define IDLE_TIMEOUT	100
#define MINUTES		(60 * 1000) /* convert ms to minutes */

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

#define JSARRAY		"\"%s\":["
#define JSEMPTYARRAY	"\"%s\":[]"
#define JSSTR		"\"%s\":\"%s\""
#define JSINT		"\"%s\":%lld"
#define JSINDX		"\"%s\":%d"

extern int			 debug;
extern int			 stopped;
extern int			 num_interfaces;
extern struct interface		*interfaces;
extern struct list_head		*target_list;

enum { DISCONNECTED, CONNECTED };

#define  u8  __u8
#define  u16 __u16
#define  u32 __u32
#define  u64 __u64

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

	gettimeofday(&t1, 0);

	return (t1.tv_sec - t0.tv_sec) * 1000 +
		(t1.tv_usec - t0.tv_usec) / 1000;
}

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

/*HACK*/
#define PATH_NVME_FABRICS	"/dev/nvme-fabrics"
#define PATH_NVMF_DEM_DISC	"/etc/nvme/nvmeof-dem/"
#define NUM_CONFIG_ITEMS	3
#define CONFIG_TYPE_SIZE	8
#define CONFIG_FAMILY_SIZE	8
#define CONFIG_ADDRESS_SIZE	40
#define CONFIG_PORT_SIZE	8
#define CONFIG_DEVICE_SIZE	256
#define LARGEST_TAG		8
#define LARGEST_VAL		40
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
#define PATH_NVMF_DISC		"/etc/nvme/discovery.conf"
#define PATH_NVMF_HOSTNQN	"/etc/nvme/hostnqn"
#define SYS_NVME		"/sys/class/nvme"

enum {RESTRICTED = 0, ALOW_ALL = 1};

struct interface {
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	int			 addr[ADDR_LEN];
	char			 pseudo_target_port[CONFIG_PORT_SIZE + 1];
	struct xp_pep		*listener;
	struct xp_ops		*ops;
};

struct host {
	struct list_head	 node;
	struct subsystem	*subsystem;
	char			 nqn[MAX_NQN_SIZE + 1];
	int			 access;
};

struct subsystem {
	struct list_head	 node;
	struct list_head	 host_list;
	struct target		*target;
	char			 nqn[MAX_NQN_SIZE + 1];
	int			 access;
	int			 log_page_valid;
	struct nvmf_disc_rsp_page_entry
				 log_page;
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

struct nsdev {
	struct list_head	 node;
	char			 device[CONFIG_DEVICE_SIZE + 1];
};

struct port_id {
	struct list_head	 node;
	int			 portid;
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 family[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	char			 port[CONFIG_PORT_SIZE + 1];
	int			 port_num;
	int			 addr[ADDR_LEN];
};

struct target {
	struct list_head	 node;
	struct list_head	 subsys_list;
	struct list_head	 portid_list;
	struct list_head	 device_list;
	struct interface	*iface;
	struct endpoint		 dq;
	char			 alias[MAX_ALIAS_SIZE + 1];
	int			 dq_connected;
	int			 mgmt_mode;
	int			 refresh;
	int			 log_page_failed;
	int			 refresh_countdown;
	int			 kato_countdown;
	int			 num_subsystems;
	int			 dirty;
};

enum { LOCAL_MGMT = 0, IN_BAND_MGMT, OUT_OF_BAND_MGMT };

struct mg_connection;

void shutdown_dem(void);
void handle_http_request(struct mg_connection *c, void *ev_data);

int init_json(char *filename);
void cleanup_json(void);
void json_spinlock(void);
void json_spinunlock(void);

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
int start_pseudo_target(struct interface *iface);
int run_pseudo_target(struct endpoint *ep, void *id);
int connect_target(struct endpoint *ep, char *family, char *addr, char *port);
void disconnect_target(struct endpoint *ep, int shutdown);
int client_connect(struct endpoint *ep, void *data, int bytes);

int send_get_log_page(struct endpoint *ep, int log_size,
		      struct nvmf_disc_rsp_page_hdr **log);
int send_keep_alive(struct endpoint *ep);
int send_get_devices(struct endpoint *ep);
int send_set_port_config(struct endpoint *ep, int len,
			 struct nvmf_port_config_page_hdr *hdr);
int send_set_subsys_config(struct endpoint *ep, int len,
			   struct nvmf_subsys_config_page_hdr *hdr);
int send_get_subsys_usage(struct endpoint *ep, int len,
			  struct nvmf_subsys_usage_rsp_page_hdr *hdr);
void fetch_log_pages(struct target *target);
int refresh_target(char *alias);
int usage_target(char *alias, char *results);
void dump(u8 *buf, int len);

#endif

/*
 * Distributed Endpoint Manager.
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

#define unlikely __glibc_unlikely

#include <sys/types.h>
#include <stdbool.h>
#include "nvme.h"	/* NOTE: Using linux kernel include here */

extern int debug;

#define BUF_SIZE	4096
#define NVMF_DQ_DEPTH	32
#define CTIMEOUT	100

#define print_debug(f, x...) do { \
	if (debug) { \
		printf("%s(%d) " f "\n", __func__, __LINE__, ##x); \
		fflush(stdout); \
	}} while (0)
#define print_info(f, x...) do { \
	printf(f "\n", ##x); \
	fflush(stdout); \
	} while (0)
#define print_err(f, x...) do { \
	fprintf(stderr, "%s(%d) Error: " f "\n", __func__, __LINE__, ##x); \
	fflush(stderr); \
	} while (0)

#define UNUSED(x) (void) x

#define min(x, y) ((x < y) ? x : y)

#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))
#endif

extern int stopped;

enum { DISCONNECTED, CONNECTED };

#define u8  __u8
#define u16 __u16
#define u32 __u32
#define u64 __u64

static inline u32 get_unaligned_le24(const u8 *p)
{
	return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16;
}

static inline u32 get_unaligned_le32(const u8 *p)
{
	return (u32) p[0] | (u32) p[1] << 8 |
		(u32) p[2] << 16 | (u32) p[3] << 24;
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
#define LARGEST_TAG		8
#define LARGEST_VAL		40

#define MAX_NQN			64

#ifndef AF_IPV4
#define AF_IPV4			1
#define AF_IPV6			2
#endif

#define IPV4_ADDR_LEN		4
#define IPV6_ADDR_LEN		8
#define FC_ADDR_LEN		8
#define IPV4_BITS		8
#define IPV6_BITS		16
#define IPV4_WIDTH		3
#define IPV6_WIDTH		4

/* HACK - Figure out which of these we need */
#define DISC_BUF_SIZE		4096
#define PATH_NVME_FABRICS	"/dev/nvme-fabrics"
#define PATH_NVMF_DISC		"/etc/nvme/discovery.conf"
#define PATH_NVMF_HOSTNQN	"/etc/nvme/hostnqn"
#define SYS_NVME		"/sys/class/nvme"

enum {NONE = 0, READ_ONLY = 1, WRITE_ONLY = 2, READ_WRITE = 3};
enum {RESTRICTED = 0, ALOW_ALL = 1};

struct host {
	struct host		*next;
	struct subsystem	*subsystem;
	char			nqn[MAX_NQN + 1];
	int			access;
};

struct subsystem {
	struct subsystem		*next;
	struct controller		*ctrl;
	char				 nqn[MAX_NQN + 1];
	struct nvmf_disc_rsp_page_entry	 log_page;
	int				 access;
	struct host			*host_list;
	int				 num_hosts;
};

struct qe {
	struct fid_mr		*recv_mr;
	u8			*buf;
};

struct context {
	struct fi_info		*prov;
	struct fi_info		*info;
	struct fid_fabric	*fab;
	struct fid_domain	*dom;
	struct fid_mr		*send_mr;
	struct fid_mr		*data_mr;
	struct fid_pep		*pep;
	struct fid_ep		*ep;
	struct fid_eq		*eq;
	struct fid_eq		*peq;
	struct fid_cq		*rcq;
	struct fid_cq		*scq;
	struct nvme_command	*cmd;
	void			*data;
	struct qe		*qe;
	int			state;
	int			csts;
	struct interface	*iface;
};

struct controller {
	struct controller	*next;
	struct interface	*interface;
	char			 trtype[CONFIG_TYPE_SIZE + 1];
	char			 addrfam[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	char			 port[CONFIG_PORT_SIZE + 1];
	int			 port_num;
	int			 addr[IPV6_ADDR_LEN];
	struct subsystem	*subsystem_list;
	int			 num_subsystems;
	struct context		 ctx;
};

struct  interface {
	int			 interface_id;
	char			 trtype[CONFIG_TYPE_SIZE + 1];
	char			 addrfam[CONFIG_FAMILY_SIZE + 1];
	char			 hostaddr[CONFIG_ADDRESS_SIZE + 1];
	int			 addr[IPV6_ADDR_LEN];
	char			 port[CONFIG_PORT_SIZE + 1];
	char			 netmask[CONFIG_ADDRESS_SIZE + 1];
	int			 mask[IPV6_ADDR_LEN];
	struct controller	*controller_list;
	int			 num_controllers;
};

struct mg_connection;

void shutdown_dem(void);
void handle_http_request(void *json_ctx, struct mg_connection *c,
			 void *ev_data);

void *init_json(char *filename);
void cleanup_json(void *context);

int get_transport(struct interface *iface, void *context);

int parse_line(FILE *fd, char *tag, int tag_max, char *value, int value_max);

int ipv4_to_addr(char *p, int *addr);
int ipv6_to_addr(char *p, int *addr);
void ipv4_mask(int *mask, int bits);
void ipv6_mask(int *mask, int bits);
int ipv4_equal(int *addr, int *dest, int *mask);
int ipv6_equal(int *addr, int *dest, int *mask);

void print_eq_error(struct fid_eq *eq, int n);

int init_interfaces(struct interface **interfaces);

int start_pseudo_target(struct context *ctx, char *addr_family, char *addr,
			char *port);
int run_pseudo_target(struct context *ctx);
int pseudo_target_check_for_host(struct context *ctx);
int connect_controller(struct context *ctx, char *addr_family, char *addr,
		       char *port);
void disconnect_controller(struct context *ctx);
void cleanup_fabric(struct context *ctx);
int send_get_log_page(struct context *ctx, int log_size,
		      struct nvmf_disc_rsp_page_hdr **log);
void fetch_log_pages(struct controller *ctrl);
int rma_read(struct fid_ep *ep, struct fid_cq *scq, void *buf, int len,
	     void *desc, u64 addr, u64 key);
int rma_write(struct fid_ep *ep, struct fid_cq *scq, void *buf, int len,
	      void *desc, u64 addr, u64 key);
int send_msg_and_repost(struct context *c, struct qe *qe, void *msg, int len);
void print_cq_error(struct fid_cq *cq, int n);
void dump(u8 *buf, int len);

// TODO make these real function since FC Bits are only 8 not 16
#define fc_to_addr	ipv6_to_addr
#define fc_mask		ipv6_mask
#define fc_equal	ipv6_equal

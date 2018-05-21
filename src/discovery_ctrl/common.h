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

/* NOTE: Using linux kernel include here */
#include "linux/list.h"
#include "linux/nvme.h"
#include "linux/kernel.h"

#include "ops.h"
#include "json.h"
#include "tags.h"
#include "dem.h"

#define JSARRAY		"\"%s\":["
#define JSEMPTYARRAY	"\"%s\":[]"
#define JSSTR		"\"%s\":\"%s\""
#define JSINT		"\"%s\":%lld"
#define JSINDX		"\"%s\":%d"
#define JSENTRY		"%s\"%s\""

extern int			 debug;
extern int			 curl_show_results;
extern int			 num_interfaces;
extern struct host_iface	*interfaces;
extern struct list_head		*target_list;
extern struct list_head		*group_list;
extern struct list_head		*host_list;

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
#define DISCOVERY_CTRL_NQN	"DEM_Discovery_Controller"

enum {RESTRICTED = 0, ALLOW_ANY = 1, UNDEFINED_ACCESS = -1};

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
	int			 treq;
	int			 adrfam;
	int			 trtype;
	int			 valid;
	int			 kato;
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

struct oob_iface {
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	int			 port;
};

struct host {
	struct list_head	 node;
	struct subsystem	*subsystem;
	char			 alias[MAX_ALIAS_SIZE + 1];
	char			 nqn[MAX_NQN_SIZE + 1];
};

struct ns {
	struct list_head	 node;
	int			 nsid;
	int			 devid;
	int			 devns;
	// TODO add bits for multipath and partitions
};

struct logpage {
	struct list_head	 node;
	struct portid		*portid;
	struct nvmf_disc_rsp_page_entry e;
	int			 valid;
};

struct subsystem {
	struct list_head	 node;
	struct list_head	 host_list;
	struct list_head	 ns_list;
	struct list_head	 logpage_list;
	struct target		*target;
	char			 nqn[MAX_NQN_SIZE + 1];
	int			 access;
};

struct nsdev {
	struct list_head	 node;
	int			 nsdev;
	int			 nsid;
	int			 valid;
	// TODO add bits for multipath and partitions
};

struct fabric_iface {
	struct list_head	 node;
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 fam[CONFIG_FAMILY_SIZE + 1];
	char			 addr[CONFIG_ADDRESS_SIZE + 1];
	int			 valid;
};

union sc_iface {
	struct oob_iface oob;
	struct ctrl_queue inb;
};

struct target {
	struct list_head	 node;
	struct list_head	 subsys_list;
	struct list_head	 portid_list;
	struct list_head	 device_list;
	struct list_head	 discovery_queue_list;
	struct list_head	 fabric_iface_list;
	struct host_iface	*iface;
	json_t			*json;
	union sc_iface		 sc_iface;
	char			 alias[MAX_ALIAS_SIZE + 1];
	int			 mgmt_mode;
	int			 refresh;
	int			 log_page_retry_count;
	int			 refresh_countdown;
	int			 kato_countdown;
	bool			 group_member;
};

struct group {
	struct list_head	 node;
	struct list_head	 target_list;
	char			 name[MAX_ALIAS_SIZE + 1];
};

struct group_target_link {
	struct list_head	 node;
	struct target		*target;
};

struct group_host_link {
	struct list_head	 node;
	struct group		*group;
	char			 alias[MAX_ALIAS_SIZE + 1];
	char			 nqn[MAX_NQN_SIZE + 1];
};

struct mg_connection;
struct mg_str;

extern struct mg_str s_signature_user;
extern struct mg_str *s_signature;

void shutdown_dem(void);
void handle_http_request(struct mg_connection *c, void *ev_data);

int init_json(char *filename);
void cleanup_json(void);
void json_spinlock(void);
void json_spinunlock(void);

int init_interfaces(void);
void *interface_thread(void *arg);

int start_pseudo_target(struct host_iface *iface);
int run_pseudo_target(struct endpoint *ep, void *id);

void build_lists(void);
struct group *init_group(char *name);
void add_host_to_group(struct group *group, char *alias);
void add_target_to_group(struct group *group, char *alias);
bool shared_group(struct target *target, char *nqn);
bool indirect_shared_group(struct target *target, char *alias);
struct target *find_target(char *alias);

void get_host_nqn(void *context, void *haddr, char *nqn);

struct subsystem *new_subsys(struct target *target, char *nqn);

void fetch_log_pages(struct ctrl_queue *dq);
void create_discovery_queue(struct subsystem *subsys, struct portid *portid);

int target_reconfig(char *alias);
int target_refresh(char *alias);
int target_usage(char *alias, char **results);
int target_logpage(char *alias, char **results);
int host_logpage(char *alias, char **results);

int set_json_nsdevs(struct target *target, char *data);
int set_json_fabric_ifaces(struct target *target, char *data);

int get_config(struct target *target);
int config_target(struct target *target);

struct target *alloc_target(char *alias);

int get_mgmt_mode(char *mode);

#endif

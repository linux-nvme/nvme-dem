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

void shutdown_dem(void);
void handle_http_request(void *json_ctx, struct mg_connection *c,
			 void *ev_data);

void *init_json(char *filename);
void cleanup_json(void *context);

extern int debug;

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

/*
 *  trtypes
 *	[NVMF_TRTYPE_RDMA]	= "rdma",
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
#define CONFIG_TYPE_SIZE	8
#define CONFIG_FAMILY_SIZE	8
#define CONFIG_ADDRESS_SIZE	64	/* TODO: ensure large enough for FC */
#define CONFIG_MAX_LINE		256
#define MAX_NQN			64	

enum {NONE = 0, READ_ONLY = 1, WRITE_ONLY = 2, READ_WRITE = 3};
enum {RESTRICTED = 0, ALOW_ALL = 1};

struct host {
	char			nqn[MAX_NQN + 1];
	int			access;
};

struct subsystem {
	char			 nqn[MAX_NQN + 1];
	int			 access;
	struct host		*host_list;
};

struct controller {
	char			 addrfam[CONFIG_FAMILY_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	struct subsystem	*subsystem_list;
	int			 num_subsystems;
	void			*log_pages;
	int			 num_logpages;
};

struct  interface {
	char			 trtype[CONFIG_TYPE_SIZE + 1];
	char			 addrfam[CONFIG_FAMILY_SIZE + 1];
	char			 hostaddr[CONFIG_ADDRESS_SIZE + 1];
	char			 netmask[CONFIG_ADDRESS_SIZE + 1];
	struct controller	*controller_list;
	int			 num_controllers;
};


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

#define _GNU_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "common.h"
#include "tags.h"

/* needs to be < NVMF_DISC_KATO in connect AND < 2 MIN for upstream target */
#define DELAY			480 /* ms */
#define KEEP_ALIVE_COUNTER	4 /* x DELAY */

// TODO customize discovery controller info
#define DEFAULT_TYPE		"rdma"
#define DEFAULT_FAMILY		"ipv4"
#define DEFAULT_ADDR		"192.168.22.2"
#define DEFAULT_PORT		"4422"

// TODO disable DEV_DEBUG before pushing to gitlab
#if 1
#define DEV_DEBUG
#endif

int				 stopped;
int				 signalled;
int				 debug;
struct discovery_queue		 discovery_queue;
struct discovery_queue		*dq = &discovery_queue;

void shutdown_dem(void)
{
	stopped = 1;
}

static void signal_handler(int sig_num)
{
	signalled = sig_num;

	shutdown_dem();
}

static int daemonize(void)
{
	pid_t			 pid, sid;

	if (getuid() != 0) {
		print_err("must be root to run demd as a daemon");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		print_err("fork failed %d", pid);
		return pid;
	}

	if (pid) /* if parent, exit to allow child to run as daemon */
		exit(0);

	umask(0022);

	sid = setsid();
	if (sid < 0) {
		print_err("setsid failed %d", sid);
		return sid;
	}

	if ((chdir("/")) < 0) {
		print_err("could not change dir to /");
		return -1;
	}

	freopen("/var/log/dem-ac_debug.log", "a", stdout);
	freopen("/var/log/dem-ac.log", "a", stderr);

	return 0;
}

static void show_help(char *app)
{
#ifdef DEV_DEBUG
	const char		*app_args = "{-q} {-d}";
#else
	const char		*app_args = "{-d} {-s}";
#endif
	const char		*dc_args =
		"{-t <typ>} {-f <fam>} {-a <adr>} {-p <port>} {-h <nqn>}";
	const char		*dc_str = "Discovery controller";

	print_info("Usage: %s %s %s", app, app_args, dc_args);
#ifdef DEV_DEBUG
	print_info("  -q - quite mode, no debug prints");
	print_info("  -d - run as a daemon process (default is standalone)");
#else
	print_info("  -d - enable debug prints in log files");
	print_info("  -s - run as a standalone process (default is daemon)");
#endif
	print_info("  -t - %s: interface type (default %s)",
		   dc_str, DEFAULT_TYPE);
	print_info("  -f - %s: address family (default %s)",
		   dc_str, DEFAULT_FAMILY);
	print_info("  -a - %s: address (default %s)", dc_str, DEFAULT_ADDR);
	print_info("  -p - %s: port/svcid (default %s)", dc_str, DEFAULT_PORT);
	print_info("  -h - HostNQN to use to connect to the %s", dc_str);
}

static int init_dem(int argc, char *argv[])
{
	struct portid		*portid;
	struct target		*target;
	int			 opt;
	int			 ret;
	int			 run_as_daemon;
#ifdef DEV_DEBUG
	const char		*opt_list = "?qdt:f:a:p:h:";
#else
	const char		*opt_list = "?dst:f:a:p:h:";
#endif

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto help;

#ifdef DEV_DEBUG
	debug = 1;
	run_as_daemon = 0;
#else
	debug = 0;
	run_as_daemon = 1;
#endif

	memset(dq, 0, sizeof(*dq));

	portid = malloc(sizeof(struct portid));
	if (!portid) {
		print_info("No memory to init");
		return 1;
	}

	target = malloc(sizeof(*target));
	if (!target) {
		print_info("No memory to init");
		return 1;
	}

	target->mgmt_mode = IN_BAND_MGMT;
	INIT_LIST_HEAD(&target->subsys_list);

	dq->portid = portid;
	dq->target = target;

	strcpy(portid->type, DEFAULT_TYPE);
	strcpy(portid->family, DEFAULT_FAMILY);
	strcpy(portid->address, DEFAULT_ADDR);
	strcpy(portid->port, DEFAULT_PORT);

	while ((opt = getopt(argc, argv, opt_list)) != -1) {
		switch (opt) {
#ifdef DEV_DEBUG
		case 'q':
			debug = 0;
			break;
		case 'd':
			run_as_daemon = 1;
			break;
#else
		case 'd':
			debug = 0;
			break;
		case 's':
			run_as_daemon = 1;
			break;
#endif
		case 't':
			if (!optarg) {
				print_info("Invalid trtype");
				goto help;
			}

			strncpy(portid->type, optarg, CONFIG_TYPE_SIZE);
			break;
		case 'f':
			if (!optarg) {
				print_info("Invalid adrfam");
				goto help;
			}

			strncpy(portid->family, optarg, CONFIG_FAMILY_SIZE);
			break;
		case 'a':
			if (!optarg) {
				print_info("Invalid traddr");
				goto help;
			}

			strncpy(portid->address, optarg, CONFIG_ADDRESS_SIZE);
			break;
		case 'p':
			if (!optarg) {
				print_info("Invalid trsvcid");
				goto help;
			}

			strncpy(portid->port, optarg, CONFIG_PORT_SIZE);
			break;
		case 'h':
			if (!optarg) {
				print_info("Invalid hostnqn");
				goto help;
			}

			strncpy(dq->hostnqn, optarg, MAX_NQN_SIZE);
			break;
		case '?':
		default:
help:
			show_help(argv[0]);
			return 1;
		}
	}

	if (portid->type) {
		if (strcmp(portid->type, TRTYPE_STR_RDMA) == 0)
			dq->ep.ops = rdma_register_ops();
	}

	if (!dq->ep.ops) {
		print_info("Invalid trtype: valid options %s",
			   TRTYPE_STR_RDMA);
		goto help;
	}

	if (!portid->family) {
		print_info("Missing adrfam");
		goto help;
	}

	if (strcmp(portid->family, ADRFAM_STR_IPV4) == 0)
		portid->adrfam = NVMF_ADDR_FAMILY_IP4;
	else if (strcmp(portid->family, ADRFAM_STR_IPV6) == 0)
		portid->adrfam = NVMF_ADDR_FAMILY_IP6;
	else if (strcmp(portid->family, ADRFAM_STR_FC) == 0)
		portid->adrfam = NVMF_ADDR_FAMILY_FC;

	if (!portid->adrfam) {
		print_info("Invalid adrfam: valid options %s, %s, %s",
			   ADRFAM_STR_IPV4, ADRFAM_STR_IPV6, ADRFAM_STR_FC);
		goto help;
	}

	if (!portid->address) {
		print_info("Missing traddr");
		goto help;
	}

	switch (portid->adrfam) {
	case NVMF_ADDR_FAMILY_IP4:
		ret = ipv4_to_addr(portid->address, portid->addr);
		break;
	case NVMF_ADDR_FAMILY_IP6:
		ret = ipv6_to_addr(portid->address, portid->addr);
		break;
	case NVMF_ADDR_FAMILY_FC:
		ret = fc_to_addr(portid->address, portid->addr);
		break;
	}

	if (ret) {
		print_info("Invalid traddr");
		goto help;
	}

	if (portid->port)
		portid->port_num = atoi(portid->port);

	if (!portid->port_num) {
		print_info("Invalid trsvcid");
		goto help;
	}

	if (strlen(dq->hostnqn) == 0) {
		uuid_t		id;
		char		uuid[40];

		uuid_generate(id);
		uuid_unparse_lower(id, uuid);
		sprintf(dq->hostnqn, NVMF_UUID_FMT, uuid);
	}

	if (run_as_daemon)
		if (daemonize())
			return 1;

	return 0;
}

static inline void invalidate_log_pages(struct target *target)
{
	struct subsystem		*subsys;
	struct logpage			*logpage;

	list_for_each_entry(subsys, &target->subsys_list, node)
		list_for_each_entry(logpage, &subsys->logpage_list, node)
			logpage->valid = 0;
}

static inline int match_logpage(struct logpage *logpage,
				struct nvmf_disc_rsp_page_entry *e)
{
	if (strcmp(e->traddr, logpage->e.traddr) ||
	    strcmp(e->trsvcid, logpage->e.trsvcid) ||
	    e->trtype != logpage->e.trtype ||
	    e->adrfam != logpage->e.adrfam)
		return 0;
	return 1;
}

static inline void store_logpage(struct logpage *logpage,
				 struct nvmf_disc_rsp_page_entry *e,
				 struct discovery_queue *dq)
{
	logpage->e = *e;
	logpage->valid = 1;
	logpage->portid = dq->portid;
}

static void save_log_pages(struct nvmf_disc_rsp_page_hdr *log, int numrec,
			   struct target *target, struct discovery_queue *dq)
{
	int				 i;
	int				 found;
	struct subsystem		*subsys;
	struct logpage			*logpage;
	struct nvmf_disc_rsp_page_entry *e;

	for (i = 0; i < numrec; i++) {
		e = &log->entries[i];
		found = 0;
		list_for_each_entry(subsys, &target->subsys_list, node)
			if ((strcmp(subsys->nqn, e->subnqn) == 0)) {
				found = 1;
				list_for_each_entry(logpage,
						    &subsys->logpage_list,
						    node) {
					if (match_logpage(logpage, e)) {
						store_logpage(logpage, e, dq);
						goto next;
					}
				}

next:
				break;
			}

		if (!found) {
			subsys = malloc(sizeof(*subsys));
			if (!subsys) {
				print_err("alloc new subsys failed");
				return;
			}

			INIT_LIST_HEAD(&subsys->logpage_list);

			strcpy(subsys->nqn, e->subnqn);

			print_debug("added subsystem %s to target %s",
				    e->subnqn, target->alias);
		}

		logpage = malloc(sizeof(*logpage));
		if (!logpage) {
			print_err("alloc new logpage failed");
			return;
		}

		store_logpage(logpage, e, dq);

		list_add_tail(&logpage->node, &subsys->logpage_list);
	}
}

void fetch_log_pages(struct discovery_queue *dq)
{
	struct nvmf_disc_rsp_page_hdr	*log = NULL;
	struct target			*target = dq->target;
	u32				 num_records = 0;

	if (get_logpages(dq, &log, &num_records)) {
		print_err("get logpages for target %s failed", target->alias);
		return;
	}

	invalidate_log_pages(target);

	save_log_pages(log, num_records, target, dq);

	print_discovery_log(log, num_records);

	free(log);
}

static void cleanup_dq(void)
{
	struct subsystem	*subsys, *next_subsys;
	struct logpage		*logpage, *next_logpage;

	if (dq->connected)
		disconnect_target(&dq->ep, 0);

	free(dq->portid);

	list_for_each_entry_safe(subsys, next_subsys,
				 &dq->target->subsys_list, node) {
		list_for_each_entry_safe(logpage, next_logpage,
					 &subsys->logpage_list, node)
			free(logpage);

		free(subsys);
	}
}

int main(int argc, char *argv[])
{
	int			 ret = 1;
	int			 cnt = 0;

	if (init_dem(argc, argv))
		goto out;

	if (connect_target(dq))
		print_info("Unable to connect to Discovery controller");
	else {
		fetch_log_pages(dq);
		dq->connected = CONNECTED;
	}

	signalled = stopped = 0;

	print_info("Starting server");

	while (!stopped) {
		usleep(DELAY);
		if (!dq->connected) {
			if (!connect_target(dq)) {
				fetch_log_pages(dq);
				dq->connected = CONNECTED;
				cnt = 0;
			}
		}
		if (dq->connected && (++cnt > KEEP_ALIVE_COUNTER)) {
			send_keep_alive(&dq->ep);
			cnt = 0;
		}
	}

	if (signalled)
		printf("\n");

	cleanup_dq();

	ret = 0;
out:
	return ret;
}

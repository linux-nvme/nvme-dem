/*
 * NVMe over Fabrics Distributed Endpoint Management (NVMe-oF DEM).
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
#include <dirent.h>
#include <sys/stat.h>

#include "common.h"
#include "tags.h"

// TODO disable DEV_DEBUG before pushing to gitlab
#if 1
#define DEV_DEBUG
#endif

static int			 run_as_daemon;
int				 stopped;
static int			 signalled;
static int			 debug;
static struct discovery_queue	 discovery_queue;

static const char *arg_str(const char * const *strings, size_t array_size,
			   size_t idx)
{
	if (idx < array_size && strings[idx])
		return strings[idx];

	return "unrecognized";
}

static const char * const trtypes[] = {
	[NVMF_TRTYPE_RDMA]	= "rdma",
	[NVMF_TRTYPE_FC]	= "fibre-channel",
	[NVMF_TRTYPE_LOOP]	= "loop",
};

static const char *trtype_str(u8 trtype)
{
	return arg_str(trtypes, ARRAY_SIZE(trtypes), trtype);
}

static void shutdown_dem(void)
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

static int init_dq(struct discovery_queue *dq)
{
	struct portid		*portid;
	struct target		*target;

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

	strcpy(target->alias, "dem-ac");

	target->mgmt_mode = IN_BAND_MGMT;
	INIT_LIST_HEAD(&target->subsys_list);

	dq->portid = portid;
	dq->target = target;

	strcpy(portid->type, DEFAULT_TYPE);
	strcpy(portid->family, DEFAULT_FAMILY);
	strcpy(portid->address, DEFAULT_ADDR);
	strcpy(portid->port, DEFAULT_PORT);

	return 0;
}

static int parse_args(int argc, char *argv[], struct discovery_queue *dq)
{
	struct portid		*portid = dq->portid;
	int			 opt;
#ifdef DEV_DEBUG
	const char		*opt_list = "?qdt:f:a:p:h:";
#else
	const char		*opt_list = "?dst:f:a:p:h:";
#endif

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto out;

#ifdef DEV_DEBUG
	debug = 1;
	run_as_daemon = 0;
#else
	debug = 0;
	run_as_daemon = 1;
#endif

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
				goto out;
			}

			strncpy(portid->type, optarg, CONFIG_TYPE_SIZE);
			break;
		case 'f':
			if (!optarg) {
				print_info("Invalid adrfam");
				goto out;
			}

			strncpy(portid->family, optarg, CONFIG_FAMILY_SIZE);
			break;
		case 'a':
			if (!optarg) {
				print_info("Invalid traddr");
				goto out;
			}

			strncpy(portid->address, optarg, CONFIG_ADDRESS_SIZE);
			break;
		case 'p':
			if (!optarg) {
				print_info("Invalid trsvcid");
				goto out;
			}

			strncpy(portid->port, optarg, CONFIG_PORT_SIZE);
			break;
		case 'h':
			if (!optarg) {
				print_info("Invalid hostnqn");
				goto out;
			}

			strncpy(dq->hostnqn, optarg, MAX_NQN_SIZE);
			break;
		case '?':
		default:
			goto out;
		}
	}
	return 0;
out:
	return 1;
}

static int validate_dq(struct discovery_queue *dq)
{
	struct portid		*portid = dq->portid;
	int			 ret;

	if (portid->type) {
		if (strcmp(portid->type, TRTYPE_STR_RDMA) == 0)
			dq->ep.ops = rdma_register_ops();
	}

	if (!dq->ep.ops) {
		print_info("Invalid trtype: valid options %s",
			   TRTYPE_STR_RDMA);
		goto out;
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
		goto out;
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
		goto out;
	}

	if (portid->port)
		portid->port_num = atoi(portid->port);

	if (!portid->port_num) {
		print_info("Invalid trsvcid");
		goto out;
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
			goto out;
	return 0;
out:
	return 1;
}

static int validate_usage(void)
{
	FILE			*fd;

	if (getuid() != 0) {
		print_info("must be root to allow access to %s",
			   NVME_FABRICS_DEV);
		goto out;
	}

	fd = fopen(NVME_FABRICS_DEV, "r");
	if (!fd) {
		print_info("nvme-fabrics kernel module must be loaded");
		goto out;
	}
	fclose(fd);

	return 0;
out:
	return 1;
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

			list_add_tail(&subsys->node, &target->subsys_list);

			INIT_LIST_HEAD(&subsys->logpage_list);

			strcpy(subsys->nqn, e->subnqn);

			print_debug("added subsystem '%s'", e->subnqn);
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

static void fetch_log_pages(struct discovery_queue *dq)
{
	struct nvmf_disc_rsp_page_hdr	*log = NULL;
	struct target			*target = dq->target;
	u32				 num_records = 0;

	if (get_logpages(dq, &log, &num_records)) {
		print_err("get logpages for hostnqn %s failed", dq->hostnqn);
		return;
	}

	invalidate_log_pages(target);

	save_log_pages(log, num_records, target, dq);

	print_discovery_log(log, num_records);

	free(log);
}


static void connect_subsystems(struct discovery_queue *dq)
{
	struct subsystem	*subsys;
	struct logpage		*logpage;
	struct dirent		*entry;
	DIR			*dir;
	FILE			*fd;
	char			 path[FILENAME_MAX + 1];
	char			 val[MAX_NQN_SIZE + 1];
	char			 address[MAX_ADDR_SIZE + 1];
	char			*port;
	char			*addr;
	int			 pos;

	list_for_each_entry(subsys, &dq->target->subsys_list, node) {
		list_for_each_entry(logpage, &subsys->logpage_list, node)
			logpage->connected = 0;

		dir = opendir(SYS_CLASS_PATH);

		for_each_dir(entry, dir) {
			if (strncmp(entry->d_name, "nvme", 4))
				continue;

			pos = sprintf(path, "%s/%s/",
				      SYS_CLASS_PATH, entry->d_name);

			strcpy(path + pos, SYS_CLASS_SUBNQN_FILE);
			fd = fopen(path, "r");
			fgets(val, sizeof(val), fd);
			fclose(fd);
			*strchrnul(val, '\n') = 0;
			if (strcmp(subsys->nqn, val))
				continue;

			strcpy(path + pos, SYS_CLASS_ADDR_FILE);
			fd = fopen(path, "r");
			fgets(address, sizeof(address), fd);
			fclose(fd);
			*strchrnul(address, '\n') = 0;

			strcpy(path + pos, SYS_CLASS_TRTYPE_FILE);
			fd = fopen(path, "r");
			fgets(val, sizeof(val), fd);
			fclose(fd);
			*strchrnul(val, '\n') = 0;

			addr = index(address, '=') + 1;
			port = index(addr, ',');
			*port++ = 0;
			port = index(port, '=') + 1;

			list_for_each_entry(logpage, &subsys->logpage_list,
					    node) {
				if (strcmp(trtype_str(logpage->e.trtype), val))
					continue;
				if (strcmp(logpage->e.traddr, addr))
					continue;
				if (strcmp(logpage->e.trsvcid, port))
					continue;
				logpage->connected = 1;
				print_info("subsys %s already %s",
					   subsys->nqn, entry->d_name);
				break;
			}
		}

		closedir(dir);

		list_for_each_entry(logpage, &subsys->logpage_list, node) {
			if (logpage->connected)
				continue;
			fd = fopen(PATH_NVME_FABRICS, "w");
			fprintf(fd, NVME_FABRICS_FMT,
				trtype_str(logpage->e.trtype),
				logpage->e.traddr, logpage->e.trsvcid,
				subsys->nqn, dq->hostnqn);
			fclose(fd);
			print_info("subsys %s connected", subsys->nqn);
		}
	}
}

static void cleanup_dq(struct discovery_queue *dq)
{
	struct subsystem	*subsys, *next_subsys;
	struct logpage		*logpage, *next_logpage;

	if (dq->connected) {
		disconnect_target(&dq->ep, 0);
		dq->connected = DISCONNECTED;
	}

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
	struct discovery_queue	*dq = &discovery_queue;
	const char		*dc_str = "Discovery controller";

	if (init_dq(dq))
		goto out;

	if (parse_args(argc, argv, dq)) {
		show_help(argv[0]);
		goto out;
	}
	if (validate_dq(dq)) {
		show_help(argv[0]);
		goto out;
	}

	if (validate_usage())
		goto out;

	if (connect_target(dq))
		print_info("Unable to connect to %s", dc_str);
	else {
		print_info("Connected to %s", dc_str);
		usleep(100);
		fetch_log_pages(dq);
		connect_subsystems(dq);
		dq->connected = CONNECTED;
	}

	signalled = stopped = 0;

	print_info("Starting server");

	while (!stopped) {
		usleep(DELAY);
		if (!dq->connected) {
			if (!connect_target(dq)) {
				print_info("Connected to %s", dc_str);
				cnt = 0;
				usleep(100);
				fetch_log_pages(dq);
				connect_subsystems(dq);
				dq->connected = CONNECTED;
			}
		}
		if (dq->connected && (++cnt > KEEP_ALIVE_COUNTER)) {
			cnt = 0;
			ret = send_keep_alive(&dq->ep);
			if (ret) {
				print_err("Lost connection to %s", dc_str);
				cleanup_dq(dq);
			}
		}
	}

	if (signalled)
		printf("\n");

	free(dq->portid);

	cleanup_dq(dq);

	ret = 0;
out:
	return ret;
}

// SPDX-License-Identifier: DUAL GPL-2.0/BSD
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

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#include "common.h"
#include "tags.h"
#include "dem.h"

static int			 run_as_daemon;
int				 stopped;
static int			 signalled;
int				 debug;
static struct ctrl_queue	 discovery_queue;
static const char		*dc_str = "Discovery controller";
static const char		*divider = "--------------------------------";

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

	freopen("/var/log/dem-hac_debug.log", "a", stdout);
	freopen("/var/log/dem-hac.log", "a", stderr);

	return 0;
}

static void show_help(char *app)
{
#ifdef DEV_DEBUG
	const char		*app_args = "{-q} {-d}";
#else
	const char		*app_args = "{-d} {-S}";
#endif
	const char		*hac_args = "{-h <hostnqn>}";
	const char		*dc_args =
		"{-t <tryp>} {-f <adrfam>} {-a <traddr>} {-s <trsvcid>}";

	print_info("Usage: %s %s %s\n\t%s", app, app_args, hac_args, dc_args);
#ifdef DEV_DEBUG
	print_info("  -q - quite mode, no debug prints");
	print_info("  -d - run as a daemon process (default is standalone)");
#else
	print_info("  -d - enable debug prints in log files");
	print_info("  -S - run as a standalone process (default is daemon)");
#endif
	print_info("  -h - HostNQN to use to connect to the %s", dc_str);
	print_info("%s info:", dc_str);
	print_info("  -t - transport type (default %s)", DEFAULT_TYPE);
	print_info("  -f - address family (default %s)", DEFAULT_FAMILY);
	print_info("  -a - transport address (default %s)", DEFAULT_ADDR);
	print_info("  -s - transport sevice id (default %s)", DEFAULT_PORT);
}

static int init_dq(struct ctrl_queue *dq)
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
		free(portid);
		return 1;
	}

	strcpy(target->alias, "dem-hac");

	target->mgmt_mode = DISCOVERY_CTRL;
	INIT_LINKED_LIST(&target->subsys_list);

	dq->portid = portid;
	dq->target = target;

	strcpy(portid->type, DEFAULT_TYPE);
	strcpy(portid->family, DEFAULT_FAMILY);
	strcpy(portid->address, DEFAULT_ADDR);
	strcpy(portid->port, DEFAULT_PORT);

	return 0;
}

static int parse_args(int argc, char *argv[], struct ctrl_queue *dq)
{
	struct portid		*portid = dq->portid;
	int			 opt;
#ifdef DEV_DEBUG
	const char		*opt_list = "?qdt:f:a:s:h:";
#else
	const char		*opt_list = "?dSt:f:a:s:h:";
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
		case 'S':
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
		case 's':
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

static void cleanup_dq(struct ctrl_queue *dq)
{
	struct subsystem	*subsys, *next_subsys;
	struct logpage		*logpage, *next_logpage;

	if (dq->connected)
		disconnect_ctrl(dq, 0);

	list_for_each_entry_safe(subsys, next_subsys,
				 &dq->target->subsys_list, node) {
		list_for_each_entry_safe(logpage, next_logpage,
					 &subsys->logpage_list, node) {
			list_del(&logpage->node);
			free(logpage);
		}

		list_del(&subsys->node);
		free(subsys);
	}
}

static int validate_dq(struct ctrl_queue *dq)
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
	default:
		ret = -EINVAL;
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
		char		uuid[UUID_LEN + 1];

		gen_uuid(uuid);
		sprintf(dq->hostnqn, NVMF_UUID_FMT, uuid);
	}

	if (run_as_daemon)
		if (daemonize())
			goto out;
	return 0;
out:
	return 1;
}

static inline void invalidate_log_pages(struct target *target)
{
	struct subsystem	*subsys;
	struct logpage		*logpage;

	list_for_each_entry(subsys, &target->subsys_list, node)
		list_for_each_entry(logpage, &subsys->logpage_list, node)
			logpage->valid = DELETED_LOGPAGE;
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
				 struct ctrl_queue *dq)
{
	logpage->e = *e;
	logpage->valid = NEW_LOGPAGE;
	logpage->portid = dq->portid;
}

static void save_log_pages(struct nvmf_disc_rsp_page_hdr *log, int numrec,
			   struct target *target, struct ctrl_queue *dq)
{
	int			 i;
	int			 found;
	struct subsystem	*subsys;
	struct logpage		*logpage;
	struct nvmf_disc_rsp_page_entry *e;

	for (i = 0; i < numrec; i++) {
		e = &log->entries[i];
		found = 0;
		list_for_each_entry(subsys, &target->subsys_list, node)
			if (!strcmp(subsys->nqn, e->subnqn)) {
				found = 1;
				list_for_each_entry(logpage,
						    &subsys->logpage_list,
						    node) {
					if (match_logpage(logpage, e)) {
						logpage->valid = VALID_LOGPAGE;
						goto next;
					}
				}
				break;
			}

		if (!found) {
			subsys = malloc(sizeof(*subsys));
			if (!subsys) {
				print_err("alloc new subsys failed");
				return;
			}

			list_add_tail(&subsys->node, &target->subsys_list);

			INIT_LINKED_LIST(&subsys->logpage_list);

			strcpy(subsys->nqn, e->subnqn);

			print_info("added subsystem '%s'", e->subnqn);
		}

		logpage = malloc(sizeof(*logpage));
		if (!logpage) {
			print_err("alloc new logpage failed");
			return;
		}

		store_logpage(logpage, e, dq);

		list_add_tail(&logpage->node, &subsys->logpage_list);
next:
	continue;
	}
}

static void fetch_log_pages(struct ctrl_queue *dq)
{
	struct nvmf_disc_rsp_page_hdr *log = NULL;
	struct target		*target = dq->target;
	u32			 num_records = 0;

	if (get_logpages(dq, &log, &num_records)) {
		print_err("get logpages for hostnqn %s failed", dq->hostnqn);
		return;
	}

	invalidate_log_pages(target);

	save_log_pages(log, num_records, target, dq);

	if (num_records)
		free(log);
}

static void print_log_pages(struct ctrl_queue *dq)
{
	struct subsystem	*subsys, *s;
	struct logpage		*log, *l;
	struct nvmf_disc_rsp_page_hdr *hdr;
	int			 bytes = sizeof(*hdr) + sizeof(log->e);

	hdr = malloc(bytes);
	if (!hdr)
		return;

	memset(hdr, 0, bytes);

	list_for_each_entry_safe(subsys, s, &dq->target->subsys_list, node) {
		list_for_each_entry_safe(log, l, &subsys->logpage_list, node) {
			if (log->valid == NEW_LOGPAGE) {
				hdr->entries[0] = log->e;
				print_info("%s", divider);
				print_discovery_log(hdr, 1);
			} else if (log->valid == DELETED_LOGPAGE) {
				print_info("subsys '%s' on %s %s %s deleted",
					   subsys->nqn,
					   trtype_str(log->e.trtype),
					   log->e.traddr, log->e.trsvcid);
				list_del(&log->node);
				free(log);
			}
		}

		if (list_empty(&subsys->logpage_list)) {
			print_info("deleted subsystem '%s', no log pages",
				   subsys->nqn);
			list_del(&subsys->node);
			free(subsys);
		}
	}

	free(hdr);
}

static void cleanup_log_pages(struct ctrl_queue *dq)
{
	struct subsystem	*subsys;
	struct logpage		*logpage, *next;

	list_for_each_entry(subsys, &dq->target->subsys_list, node)
		list_for_each_entry_safe(logpage, next,
					 &subsys->logpage_list, node)
			if (logpage->valid == DELETED_LOGPAGE) {
				list_del(&logpage->node);
				free(logpage);
			}
}

static int enable_aens(struct ctrl_queue *dq)
{
	int			 ret;
	u64			 result;

	ret = send_get_features(&dq->ep, NVME_FEAT_ASYNC_EVENT, &result);
	if (ret) {
		print_err("get AEN feature failed");
		return ret;
	}

	if (!result) {
		print_err("AENs not supported.");
		return -EINVAL;
	}

	ret = send_set_features(&dq->ep, NVME_FEAT_ASYNC_EVENT,
				NVME_AEN_CFG_DISC_LOG_CHG);
	if (ret) {
		print_err("set AEN feature failed");
		return ret;
	}

	send_async_event_request(&dq->ep);

	return 0;
}

static inline void report_updates(struct ctrl_queue *dq)
{
	fetch_log_pages(dq);
	print_log_pages(dq);
	cleanup_log_pages(dq);

	if (!dq->failed_kato)
		enable_aens(dq);
}

static inline int complete_connection(struct ctrl_queue *dq)
{
	dq->connected = CONNECTED;
	usleep(100);
	if (stopped)
		return -ESHUTDOWN;

	report_updates(dq);

	if (dq->failed_kato) {
		disconnect_ctrl(dq, 0);
		dq->connected = DISCONNECTED;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int			 ret = 1;
	int			 cnt = 0;
	struct ctrl_queue	*dq = &discovery_queue;

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

	signalled = stopped = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	if (connect_ctrl(dq))
		print_info("Unable to connect to %s", dc_str);
	else if (complete_connection(dq))
		goto cleanup;

	while (!stopped) {
		if (!dq->connected) {
			usleep(DELAY);

			if (++cnt < CONNECT_RETRY_COUNTER)
				continue;

			if (!connect_ctrl(dq)) {
				cnt = 0;
				if (complete_connection(dq))
					break;

				if (stopped)
					break;
			}
		}
		if (dq->connected) {
			if (!process_nvme_rsp(&dq->ep, 0, NULL)) {
				print_info("%s", divider);
				print_info("Received AEN");
				report_updates(dq);
				cnt = 0;
			} else if (++cnt > KEEP_ALIVE_COUNTER) {
				cnt = 0;
				ret = send_keep_alive(&dq->ep);
				if (stopped)
					break;
				if (ret) {
					print_err("Lost connection to %s",
						  dc_str);
					cleanup_dq(dq);
				}
			}
		}
	}

cleanup:
	cleanup_dq(dq);

	if (signalled)
		printf("\n");

	free(dq->portid);

	print_info("Shutting down");

	ret = 0;
out:
	return ret;
}

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
#include <stdbool.h>
#include <stdio.h>

#include "common.h"
#include "tags.h"
#include "ops.h"

static inline u8 to_trtype(char *str)
{
	if (strcmp(str, TRTYPE_STR_RDMA) == 0)
		return NVMF_TRTYPE_RDMA;
	if (strcmp(str, TRTYPE_STR_FC) == 0)
		return NVMF_TRTYPE_FC;
#if 0
	if (strcmp(str, TRTYPE_STR_TCP) == 0)
		return NVME_TRTYPE_TCP;
#endif
	return 0;
}

static inline u8 to_adrfam(char *str)
{
	if (strcmp(str, ADRFAM_STR_IPV4) == 0)
		return NVMF_ADDR_FAMILY_IP4;
	if (strcmp(str, ADRFAM_STR_IPV6) == 0)
		return NVMF_ADDR_FAMILY_IP6;
	if (strcmp(str, ADRFAM_STR_FC) == 0)
		return NVMF_ADDR_FAMILY_FC;
	return 0;
}

static int build_port_config_data(struct target *target,
				  struct nvmf_port_config_page_hdr **_hdr)
{
	struct nvmf_port_config_page_entry *entry;
	struct nvmf_port_config_page_hdr *hdr;
	struct port_id		*port_id;
	void			*ptr;
	int			 len = 0;
	int			 count = 0;

	list_for_each_entry(port_id, &target->portid_list, node)
		count++;

	len = sizeof(hdr) - 1 + (count * sizeof(*entry));
	if (posix_memalign(&ptr, PAGE_SIZE, len)) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	hdr = ptr;
	hdr->num_entries = count;

	entry = (struct nvmf_port_config_page_entry *) &hdr->data;
	list_for_each_entry(port_id, &target->portid_list, node) {
		entry->status = 0;
		entry->portid = port_id->portid;
		entry->trtype = to_trtype(port_id->type);
		entry->adrfam = to_adrfam(port_id->family);
		strcpy(entry->traddr, port_id->address);
		strcpy(entry->trsvcid, port_id->port);
		entry++;
	}

	*_hdr = hdr;

	return len;
}

static int build_subsys_config_data(struct target *target,
				    struct nvmf_subsys_config_page_hdr **_hdr)
{
	struct nvmf_subsys_config_page_entry *entry;
	struct nvmf_subsys_config_page_hdr *hdr;
	struct subsystem	*subsystem;
	struct host		*host;
	char			*hostnqn;
	void			*ptr;
	int			 len = 0;
	int			 count = 0;

	len = sizeof(hdr) - 1;
	list_for_each_entry(subsystem, &target->subsys_list, node) {
		len += sizeof(*entry) - 1;
		count++;
		list_for_each_entry(host, &subsystem->host_list, node)
			len += NVMF_NQN_FIELD_LEN;
	}

	if (posix_memalign(&ptr, PAGE_SIZE, len)) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	hdr = ptr;
	hdr->num_entries = count;

	entry = (struct nvmf_subsys_config_page_entry *) &hdr->data;
	list_for_each_entry(subsystem, &target->subsys_list, node) {
		entry->status = 0;
		entry->allowallhosts = subsystem->access;
		strcpy(entry->subnqn, subsystem->nqn);
		count = 0;
		hostnqn = (char *) &entry->data;
		list_for_each_entry(host, &subsystem->host_list, node) {
			strcpy(hostnqn, host->nqn);
			count++;
			hostnqn += NVMF_NQN_FIELD_LEN;
		}
		entry->numhosts = count;
		entry = (struct nvmf_subsys_config_page_entry *) hostnqn;
	}

	*_hdr = hdr;

	return len;
}

void init_targets(void)
{
//	struct nvmf_port_config_page_hdr *port_hdr;
//	struct nvmf_subsys_config_page_hdr *subsys_hdr;
	struct target		*target;
	struct port_id		*portid;
//	int			 len;
	int			 ret;

	build_target_list();

	list_for_each_entry(target, target_list, node) {
		// TODO walk interface list to find portid we can use

		portid = list_first_entry(&target->portid_list,
					  struct port_id, node);

		target->log_page_failed = 1;

		if (strcmp(portid->type, "rdma") == 0)
			target->dq.ops = rdma_register_ops();
		else
			continue;

		ret = connect_target(&target->dq, portid->family,
				     portid->address, portid->port);
		if (ret) {
			print_err("Could not connect to target %s",
				  target->alias);
			continue;
		}

		target->dq_connected = 1;

#if 0
// TODO Do before (or?) after fetch_log_pages. Should this be worker thread?
		if (target->mgmt_mode == IN_BAND_MGMT) {
			// TODO build/send device/transport cmd
			len = build_port_config_data(target, &port_hdr);
			if (!len)
				print_err("build port config failed for %s",
					  target->alias);

			ret = send_set_port_config(&target->dq, len, port_hdr);
			if (ret)
				print_err("send port config failed for %s",
					  target->alias);

			len = build_subsys_config_data(target, &subsys_hdr);
			if (!len)
				print_err("build subsys config failed for %s",
					  target->alias);

			ret = send_set_subsys_config(&target->dq, len,
						     subsys_hdr);
			if (ret)
				print_err("send subsys config failed for %s",
					  target->alias);
		} else if (target->mgmt_mode == OUT_OF_BAND_MGMT) {
			// TODO get request to demt for dev/xport data
			// TODO put request to demt to configure
		}
#endif
		fetch_log_pages(target);

		target->refresh_countdown =
			target->refresh * MINUTES / IDLE_TIMEOUT;
		target->log_page_failed = 0;

		if (target->mgmt_mode != IN_BAND_MGMT) {
			disconnect_target(&target->dq, 0);
			target->dq_connected = 0;
		}
	}
}

void cleanup_targets(void)
{
	struct target		*target;
	struct target		*next_target;
	struct subsystem	*subsys;
	struct subsystem	*next_subsys;
	struct host		*host;
	struct host		*next_host;

	list_for_each_entry_safe(target, next_target, target_list, node) {
		list_for_each_entry_safe(subsys, next_subsys,
					 &target->subsys_list, node) {
			list_for_each_entry_safe(host, next_host,
						 &subsys->host_list, node)
				free(host);

			free(subsys);
		}

		list_del(&target->node);

		if (target->dq_connected)
			disconnect_target(&target->dq, 0);

		free(target);
	}
}

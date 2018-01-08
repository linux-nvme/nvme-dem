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
#include "ops.h"
#include "curl.h"

static inline char *trtype_str(u8 trtype)
{
	switch (trtype) {
	case NVMF_TRTYPE_RDMA:
		return TRTYPE_STR_RDMA;
	case NVMF_TRTYPE_FC:
		return TRTYPE_STR_FC;
	case NVMF_TRTYPE_TCP:
		return TRTYPE_STR_TCP;
	default:
		return "unknown";
	}
}

static inline char *adrfam_str(u8 adrfam)
{
	switch (adrfam) {
	case NVMF_ADDR_FAMILY_IP4:
		return ADRFAM_STR_IPV4;
	case NVMF_ADDR_FAMILY_IP6:
		return ADRFAM_STR_IPV6;
	case NVMF_ADDR_FAMILY_FC:
		return ADRFAM_STR_FC;
	default:
		return "unknown";
	}
}

static inline u8 to_trtype(char *str)
{
	if (strcmp(str, TRTYPE_STR_RDMA) == 0)
		return NVMF_TRTYPE_RDMA;
	if (strcmp(str, TRTYPE_STR_FC) == 0)
		return NVMF_TRTYPE_FC;
	if (strcmp(str, TRTYPE_STR_TCP) == 0)
		return NVMF_TRTYPE_TCP;
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

static int build_set_port_inb(struct target *target,
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
		if (!port_id->valid)
			continue;

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

static int build_set_subsys_inb(struct target *target,
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

	/* TODO: do we validate subsystems? */
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

static void build_set_port_oob(struct port_id *portid, char *buf, int len)
{
	snprintf(buf, len, "{" JSSTR "," JSSTR "," JSSTR "," JSINDX "}",
		 TAG_TYPE, portid->type, TAG_FAMILY, portid->family,
		 TAG_ADDRESS, portid->address, TAG_TRSVCID, portid->port_num);
}

static void build_set_subsys_oob(struct subsystem *subsys, char *buf, int len)
{
	snprintf(buf, len, "{" JSSTR "," JSINDX "}",
		 TAG_SUBNQN, subsys->nqn, TAG_ALLOW_ANY, subsys->access);
}

int send_get_nsdevs_oob(char *addr, int port, char **buf)
{
	char			 uri[128];

	sprintf(uri, "http://%s:%d/" URI_NSDEV, addr, port);

	return exec_get(uri, buf);
}

int send_get_xports_oob(char *addr, int port, char **buf)
{
	char			 uri[128];

	sprintf(uri, "http://%s:%d/" URI_INTERFACE, addr, port);

	return exec_get(uri, buf);
}

int send_set_port_oob(struct port_id *port_id, char *addr, int port, char *buf)
{
	char			 uri[128];

	sprintf(uri, "http://%s:%d/" URI_PORTID "/%d",
		addr, port, port_id->portid);

	return exec_put(uri, buf, strlen(buf));
}

int send_set_subsys_oob(char *addr, int port, char *buf)
{
	char			 uri[128];

	sprintf(uri, "http://%s:%d/" URI_SUBSYSTEM, addr, port);
	UNUSED(buf);

	return 0;
}

static int get_oob_nsdevs(struct target *target, char *addr, int port)
{
	int			ret = 0;
	char			*nsdevs;

	ret = send_get_nsdevs_oob(addr, port, &nsdevs);
	if (ret) {
		print_err("send get nsdevs OOB failed for %s", target->alias);
		goto out1;
	}

	// TODO store info

	free(nsdevs);
out1:
	return ret;
}

static int get_oob_xports(struct target *target, char *addr, int port)
{
	int			ret = 0;
	char			*xports;

	ret = send_get_xports_oob(addr, port, &xports);
	if (ret) {
		print_err("send get xports OOB failed for %s", target->alias);
		goto out1;
	}

	// TODO store info

	free(xports);
out1:
	return ret;
}

static int get_oob_config(struct target *target)
{
	json_t			*iface = target->oob_iface;
	json_t			*obj;
	char			*addr;
	int			 port;
	int			 ret;

	json_spinlock();

	obj = json_object_get(iface, TAG_IFADDRESS);
	if (!obj || !json_is_string(obj)) {
		json_spinunlock();
		return -EINVAL;
	}
	addr = (char *) json_string_value(obj);

	obj = json_object_get(iface, TAG_IFPORT);
	if (!obj || !json_is_integer(obj)) {
		json_spinunlock();
		return -EINVAL;
	}
	port = json_integer_value(obj);

	json_spinunlock();

	ret = get_oob_nsdevs(target, addr, port);
	if (ret)
		return ret;

	return get_oob_xports(target, addr, port);
}

static int config_target_oob(struct target *target)
{
	json_t			*iface = target->oob_iface;
	json_t			*obj;
	struct port_id		*portid;
	struct subsystem	*subsys;
	char			*addr;
	int			 port;
	char			 buf[256];
	int			 ret;

	json_spinlock();

	obj = json_object_get(iface, TAG_IFADDRESS);
	if (!obj || !json_is_string(obj)) {
		json_spinunlock();
		return -EINVAL;
	}
	addr = (char *) json_string_value(obj);

	obj = json_object_get(iface, TAG_IFPORT);
	if (!obj || !json_is_integer(obj)) {
		json_spinunlock();
		return -EINVAL;
	}
	port = json_integer_value(obj);

	json_spinunlock();

	list_for_each_entry(portid, &target->portid_list, node) {
		build_set_port_oob(portid, buf, sizeof(buf));

		ret = send_set_port_oob(portid, addr, port, buf);
		if (ret)
			print_err("send set port OOB failed for %s",
				  target->alias);
	}

	list_for_each_entry(subsys, &target->subsys_list, node) {
		build_set_subsys_oob(subsys, buf, sizeof(buf));

		ret = send_set_subsys_oob(addr, port, buf);
		if (ret)
			print_err("send set subsys OOB failed for %s",
				  target->alias);
	}

	return 0;
}

static inline int get_inb_nsdevs(struct target *target)
{
	struct nvmf_ns_devices_rsp_page_hdr *nsdevs_hdr;
	struct nvmf_ns_devices_rsp_page_entry *nsdev;
	struct nsdev		*ns_dev;
	int			 i;
	int			 ret;

	ret = send_get_nsdevs(&target->dq, &nsdevs_hdr);
	if (ret) {
		print_err("send get nsdevs INB failed for %s", target->alias);
		goto out1;
	}

	if (!nsdevs_hdr->num_entries) {
		print_err("No NS devices defined for %s", target->alias);
		goto out2;
	}

	nsdev = (struct nvmf_ns_devices_rsp_page_entry *) &nsdevs_hdr->data;

	list_for_each_entry(ns_dev, &target->device_list, node)
		ns_dev->valid = 0;

	for (i = nsdevs_hdr->num_entries; i > 0; i--, nsdev++) {
		list_for_each_entry(ns_dev, &target->device_list, node)
			if (ns_dev->nsdev == nsdev->dev_id &&
			    ns_dev->nsid == nsdev->ns_id) {
				ns_dev->valid = 1;
				goto found;
		}
		if (nsdev->dev_id == 255)
			print_err("New nsdev on %s - nullb0", target->alias);
		else
			print_err("New nsdev on %s - dev id %d nsid %d",
				  target->alias, nsdev->dev_id, nsdev->ns_id);
found:
		continue;
	}

	list_for_each_entry(ns_dev, &target->device_list, node)
		if (!ns_dev->valid) {
			if (nsdev->dev_id == 255)
				print_err("Nsdev not on %s - nullb0",
					  target->alias);
			else
				print_err("Nsdev not on %s - dev id %d nsid %d",
					  target->alias, ns_dev->nsdev,
					  ns_dev->nsid);
		}
out2:
	free(nsdevs_hdr);
out1:
	return ret;
}

static inline int get_inb_xports(struct target *target)
{
	struct nvmf_transports_rsp_page_hdr *xports_hdr;
	struct nvmf_transports_rsp_page_entry *xport;
	struct port_id		*port_id;
	int			 i, rdma_found;
	int			 ret;

	ret = send_get_xports(&target->dq, &xports_hdr);
	if (ret) {
		print_err("send get xports INB failed for %s", target->alias);
		goto out1;
	}

	if (!xports_hdr->num_entries) {
		print_err("No transports defined for %s", target->alias);
		goto out2;
	}

	xport = (struct nvmf_transports_rsp_page_entry *) &xports_hdr->data;

	list_for_each_entry(port_id, &target->portid_list, node)
		port_id->valid = 0;

	for (i = xports_hdr->num_entries; i > 0; i--, xport++) {
		rdma_found = 0;
		list_for_each_entry(port_id, &target->portid_list, node) {
			if (!strcmp(xport->traddr, port_id->address) &&
			    (xport->adrfam == to_adrfam(port_id->family)) &&
			    (xport->trtype == to_trtype(port_id->type))) {
				port_id->valid = 1;
				if (xport->adrfam == NVMF_TRTYPE_RDMA)
					rdma_found = 1;
				else
					goto found;
			}
			if (!strcmp(xport->traddr, port_id->address) &&
			    (xport->adrfam == to_adrfam(port_id->family)) &&
			    (xport->trtype == NVMF_TRTYPE_RDMA &&
			     to_trtype(port_id->type) == NVMF_TRTYPE_TCP)) {
				port_id->valid = 1;
				if (!rdma_found)
					rdma_found = 1;
				else
					goto found;
			}
		}
		if (!rdma_found)
			print_err("New transport on %s - %s %s %s",
				  target->alias, trtype_str(xport->trtype),
				  adrfam_str(xport->adrfam), xport->traddr);
found:
		continue;
	}

	list_for_each_entry(port_id, &target->portid_list, node)
		if (!port_id->valid)
			print_err("Transport not on %s - %s %s %s",
				  target->alias, port_id->type,
				  port_id->family, port_id->address);
out2:
	free(xports_hdr);
out1:
	return ret;
}

static inline int get_inb_config(struct target *target)
{
	int			 ret;

	ret = get_inb_nsdevs(target);
	if (ret)
		return ret;

	return get_inb_xports(target);
}

static int config_target_inb(struct target *target)
{
	struct nvmf_port_config_page_hdr   *port_hdr = NULL;
	struct nvmf_subsys_config_page_hdr *subsys_hdr = NULL;
	int			 len;
	int			 ret = 0;

	len = build_set_port_inb(target, &port_hdr);
	if (!len)
		print_err("build set port INB failed for %s", target->alias);
	else {
		ret = send_set_port_config(&target->dq, len, port_hdr);
		if (ret) {
			print_err("send set port INB failed for %s",
				  target->alias);
			goto out;
		}
	}

	len = build_set_subsys_inb(target, &subsys_hdr);
	if (!len)
		print_err("build set subsys INB failed for %s", target->alias);
	else {
		ret = send_set_subsys_config(&target->dq, len, subsys_hdr);
		if (ret)
			print_err("send set subsys INB failed for %s",
				  target->alias);
	}

	if (port_hdr)
		free(port_hdr);
out:
	if (subsys_hdr)
		free(subsys_hdr);

	return ret;
}

void init_targets(void)
{
	struct target		*target;
	struct port_id		*portid;
	int			 ret;

	build_target_list();

	list_for_each_entry(target, target_list, node) {
		// TODO walk interface list to find portid we can use

		portid = list_first_entry(&target->portid_list,
					  struct port_id, node);

		if (strcmp(portid->type, "rdma") == 0)
			target->dq.ops = rdma_register_ops();
		else
			continue;

		ret = connect_target(target, portid->family,
				     portid->address, portid->port);
		if (ret) {
			print_err("Could not connect to target %s",
				  target->alias);
			continue;
		}

		target->dq_connected = 1;

// TODO: Should this be worker thread?
		if (target->mgmt_mode == IN_BAND_MGMT)
			ret = get_inb_config(target);
		else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
			ret = get_oob_config(target);

		fetch_log_pages(target);

		if (target->mgmt_mode == IN_BAND_MGMT && !ret)
			config_target_inb(target);
		else if (target->mgmt_mode == OUT_OF_BAND_MGMT && !ret)
			config_target_oob(target);

		target->refresh_countdown =
			target->refresh * MINUTES / IDLE_TIMEOUT;

		target->log_page_retry_count = LOG_PAGE_RETRY;

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

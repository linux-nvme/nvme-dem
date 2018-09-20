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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "common.h"

#include "ops.h"
#include "curl.h"

static const char *CONFIG_ERR    = " - unable to configure remote target";
static const char *INTERNAL_ERR  = " - unable to comply, internal error";
static const char *TARGET_ERR    = " - target not found";
static const char *NSDEV_ERR     = " - invalid ns device";
static const char *MGMT_MODE_ERR = " - invalid mgmt mode for setting interface";

/* helper function(s) */

int get_mgmt_mode(char *mode)
{
	int			 mgmt_mode;

	if (strcmp(mode, TAG_OUT_OF_BAND_MGMT) == 0)
		mgmt_mode = OUT_OF_BAND_MGMT;
	else if (strcmp(mode, TAG_IN_BAND_MGMT) == 0)
		mgmt_mode = IN_BAND_MGMT;
	else
		mgmt_mode = LOCAL_MGMT;

	return mgmt_mode;
}

struct target *find_target(char *alias)
{
	struct target		*target;

	list_for_each_entry(target, target_list, node)
		if (!strcmp(target->alias, alias))
			return target;
	return NULL;
}

static inline struct subsystem *find_subsys(struct target *target, char *nqn)
{
	struct subsystem	*subsys;

	list_for_each_entry(subsys, &target->subsys_list, node)
		if (!strcmp(subsys->nqn, nqn))
			return subsys;
	return NULL;
}

static inline struct portid *find_portid(struct target *target, int id)
{
	struct portid		*portid;

	list_for_each_entry(portid, &target->portid_list, node)
		if (portid->portid == id)
			return portid;
	return NULL;
}

static inline struct ns *find_ns(struct subsystem *subsys, int nsid)
{
	struct ns		*ns;

	list_for_each_entry(ns, &subsys->ns_list, node)
		if (ns->nsid == nsid)
			return ns;
	return NULL;
}

static inline struct group *find_group(char *name)
{
	struct group		*group;

	list_for_each_entry(group, group_list, node)
		if (!strcmp(group->name, name))
			return group;
	return NULL;
}

static inline struct group_target_link *find_group_target(struct group *group,
							  struct target *target)
{
	struct group_target_link *link;

	list_for_each_entry(link, &group->target_list, node)
		if (link->target == target)
			return link;
	return NULL;
}

static inline struct group_host_link *find_group_host(struct group *group,
						      char *nqn)
{
	struct group_host_link *link;

	list_for_each_entry(link, host_list, node)
		if (link->group == group && !strcmp(link->nqn, nqn))
			return link;
	return NULL;
}

/* notification functions */

static inline int send_notifications(struct linked_list *list)
{
	struct event_notification *entry, *next;
	struct endpoint		*ep;
	struct nvme_completion	*resp;

	list_for_each_entry_safe(entry, next, list, node) {
		ep = entry->ep;
		resp = (void *) ep->cmd;

		memset(resp, 0, sizeof(*resp));

		resp->result.U32 = NVME_AER_NOTICE_LOG_PAGE_CHANGE;

		ep->ops->send_msg(ep->ep, resp, sizeof(*resp), ep->mr);

		list_del(&entry->req->node);
		free(entry->req);
		list_del(&entry->node);
		free(entry);
	}

	return 0;
}

static inline int in_notification_list(struct linked_list *list, char *nqn)
{
	struct event_notification *entry;

	list_for_each_entry(entry, list, node)
		if (!strcmp(nqn, entry->nqn))
			return 1;

	return 0;
}

static inline void create_notification_entry(struct linked_list *list,
					     struct event_notification *req)
{
	struct event_notification *entry;

	entry = malloc(sizeof(*entry));
	if (!entry)
		return;

	memset(entry, 0, sizeof(*entry));

	strncpy(entry->nqn, req->nqn, MAX_NQN_SIZE);
	entry->ep = req->ep;
	entry->req = req;

	list_add_tail(&entry->node, list);
}

static inline void create_notification_list(struct linked_list *list)
{
	struct event_notification *req;

	INIT_LINKED_LIST(list);

	list_for_each_entry(req, aen_req_list, node)
		if (!in_notification_list(list, req->nqn))
			create_notification_entry(list, req);
}

static inline int any_subsys_unrestricted(struct target *target)
{
	struct subsystem	*subsys;

	list_for_each_entry(subsys, &target->subsys_list, node)
		if (subsys->access)
			return 1;
	return 0;
}

static inline void flag_by_group_hosts(struct group *group,
				       struct linked_list *list)
{
	struct event_notification *entry;
	struct group_host_link	*host;

	list_for_each_entry(entry, list, node)
		list_for_each_entry(host, host_list, node) {
			if ((host->group == group) &&
			    (strcmp(host->nqn, entry->nqn))) {
				entry->valid = 1;
				break;
			}
		}
}

static inline void flag_by_access_list(struct subsystem *subsys,
				       struct linked_list *list)
{
	struct event_notification *entry;
	struct host		*host;

	list_for_each_entry(entry, list, node)
		list_for_each_entry(host, &subsys->host_list, node) {
			if (strcmp(host->nqn, entry->nqn)) {
				entry->valid = 1;
				break;
			}
		}
}

static inline void prune_notification_list(struct linked_list *list)
{
	struct event_notification *entry, *next;

	list_for_each_entry_safe(entry, next, list, node)
		if (!entry->valid)
			list_del(&entry->node);
}

static inline void enable_entire_list(struct linked_list *list)
{
	struct event_notification *entry;

	list_for_each_entry(entry, list, node)
		entry->valid = 1;
}

static inline void create_event_host_list_for_subsys(struct linked_list *list,
						     struct subsystem *subsys)
{
	create_notification_list(list);

	if (list_empty(list))
		return;

	if (subsys->access) {
		enable_entire_list(list);
		return;
	}

	flag_by_access_list(subsys, list);

	prune_notification_list(list);
}

static inline void create_event_host_list_for_group(struct linked_list *list,
						    struct group *group,
						    struct target *target)
{
	struct subsystem	*subsys;

	create_notification_list(list);

	if (list_empty(list))
		return;

	flag_by_group_hosts(group, list);

	prune_notification_list(list);

	if (list_empty(list))
		return;

	if (any_subsys_unrestricted(target)) {
		enable_entire_list(list);
		return;
	}

	list_for_each_entry(subsys, &target->subsys_list, node)
		flag_by_access_list(subsys, list);

	prune_notification_list(list);
}

static inline void create_event_host_list_for_target(struct linked_list *list,
						     struct target *target)
{
	struct subsystem	*subsys;

	create_notification_list(list);

	if (list_empty(list))
		return;

	if (any_subsys_unrestricted(target)) {
		enable_entire_list(list);
		return;
	}

	list_for_each_entry(subsys, &target->subsys_list, node)
		flag_by_access_list(subsys, list);

	prune_notification_list(list);
}

static inline void create_event_host_list_for_host(struct linked_list *list,
						   char *nqn)
{
	struct event_notification *req;

	INIT_LINKED_LIST(list);

	list_for_each_entry(req, aen_req_list, node)
		if (!strcmp(nqn, req->nqn)) {
			create_notification_entry(list, req);
			break;
		}
}

/* config functions that are not send to the target */

struct group *init_group(char *name)
{
	struct group		*group;

	group = malloc(sizeof(*group));
	if (!group)
		return NULL;

	strncpy(group->name, name, MAX_ALIAS_SIZE);

	INIT_LINKED_LIST(&group->target_list);

	list_add_tail(&group->node, group_list);

	return group;
}

int add_group(char *name, char *resp)
{
	struct group		*group;
	int			 ret;

	ret = add_json_group(name, resp);
	if (ret)
		return ret;

	group = init_group(name);
	if (!group) {
		print_err("unable to alloc group");
		return -ENOMEM;
	}

	return 0;
}

int update_group(char *name, char *data, char *resp)
{
	struct group		*group;
	char			 _name[MAX_ALIAS_SIZE + 1];
	int			 ret;

	ret = update_json_group(name, data, resp, _name);
	if (ret)
		return ret;

	group = find_group(_name);
	if (!group) {
		group = init_group(_name);
		if (!group) {
			print_err("unable to alloc group");
			return -ENOMEM;
		}
	} else
		strncpy(group->name, _name, MAX_ALIAS_SIZE);

	return 0;
}

void add_host_to_group(struct group *group, char *alias)
{
	struct group_host_link	*link;
	char			 nqn[MAX_NQN_SIZE + 1];

	link = malloc(sizeof(*link));
	if (!link)
		return;

	link->group = group;

	get_json_host_nqn(alias, nqn);

	strcpy(link->alias, alias);
	strcpy(link->nqn, nqn);

	list_add_tail(&link->node, host_list);
}

void add_target_to_group(struct group *group, char *alias)
{
	struct target		*target;
	struct group_target_link *link;
	struct linked_list	 list;

	target = find_target(alias);
	if (!target)
		return;

	link = malloc(sizeof(*link));
	if (!link)
		return;

	link->target = target;
	target->group_member = true;

	list_add_tail(&link->node, &group->target_list);

	create_event_host_list_for_group(&list, group, target);
	send_notifications(&list);
}

int set_group_member(char *name, char *data, char *alias, char *tag,
		     char *parent_tag, char *resp)
{
	struct group		*group;
	char			 _alias[MAX_ALIAS_SIZE + 1];
	int			 ret;

	ret = set_json_group_member(name, data, alias, tag, parent_tag, resp,
				    _alias);
	if (ret)
		return ret;

	group = find_group(name);

	if (strcmp(tag, TAG_TARGET))
		add_host_to_group(group, _alias);
	else
		add_target_to_group(group, _alias);

	return 0;
}

static inline void del_host_from_group(struct group *group, char *alias)
{
	struct group_host_link *link;

	link = find_group_host(group, alias);
	if (!link)
		return;

	list_del(&link->node);
	free(link);
}

static inline void del_target_from_group(struct group *group, char *alias)
{
	struct target		*target;
	struct group_target_link *link;
	struct linked_list	 list;

	target = find_target(alias);
	if (!target)
		return;

	link = find_group_target(group, target);
	if (!link)
		return;

	list_del(&link->node);
	free(link);

	list_for_each_entry(group, group_list, node)
		list_for_each_entry(link, &group->target_list, node)
			if (target == link->target)
				return;

	target->group_member = false;

	create_event_host_list_for_group(&list, group, target);
	send_notifications(&list);
}

int del_group_member(char *name, char *alias, char *tag, char *parent_tag,
		     char *resp)
{
	struct group		*group;
	int			 ret;

	ret = del_json_group_member(name, alias, tag, parent_tag, resp);
	if (ret)
		return ret;

	group = find_group(name);

	if (strcmp(tag, TAG_TARGET))
		del_host_from_group(group, alias);
	else
		del_target_from_group(group, alias);

	return 0;
}

int del_group(char *name, char *resp)
{
	struct group_host_link	*link, *next;
	struct group		*group;
	int			 ret;

	ret = del_json_group(name, resp);
	if (ret)
		return ret;

	group = find_group(name);

	list_for_each_entry_safe(link, next, host_list, node)
		if (link->group == group) {
			list_del(&link->node);
			free(link);
		}

	list_del(&group->node);
	free(group);

	return 0;
}

bool shared_group(struct target *target, char *nqn)
{
	struct group_target_link *link;
	struct group_host_link	*host;

	list_for_each_entry(host, host_list, node) {
		if (strcmp(host->nqn, nqn))
			continue;
		list_for_each_entry(link, &host->group->target_list, node)
			if (link->target == target)
				return true;
	}

	return false;
}

bool indirect_shared_group(struct target *target, char *alias)
{
	struct group_target_link *link;
	struct group_host_link	*host;

	list_for_each_entry(host, host_list, node) {
		if (strcmp(host->alias, alias))
			continue;
		list_for_each_entry(link, &host->group->target_list, node)
			if (link->target == target)
				return true;
	}

	return false;
}

/* in band message formatting functions */

static int build_set_port_config_inb(struct portid *portid,
				 struct nvmf_port_config_entry **_entry)
{
	struct nvmf_port_config_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	entry->portid = portid->portid;
	entry->treq = NVMF_TREQ_NOT_REQUIRED; // TODO need to pull from portid
	entry->trtype = to_trtype(portid->type);
	entry->adrfam = to_adrfam(portid->family);
	strcpy(entry->traddr, portid->address);
	sprintf(entry->trsvcid, "%d", portid->port_num);

	*_entry = entry;

	return sizeof(*entry);
}

static int build_subsys_config_inb(struct subsystem *subsys,
				   struct nvmf_subsys_config_entry **_entry)
{
	struct nvmf_subsys_config_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	entry->allowanyhost = subsys->access;
	strncpy(entry->subnqn, subsys->nqn, MAX_NQN_SIZE);

	*_entry = entry;

	return sizeof(*entry);
}

static int build_link_host_inb(struct subsystem *subsys, struct host *host,
			       struct nvmf_link_host_entry **_entry)
{
	struct nvmf_link_host_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	strncpy(entry->subnqn, subsys->nqn, MAX_NQN_SIZE);
	strncpy(entry->hostnqn, host->nqn, MAX_NQN_SIZE);

	*_entry = entry;

	return sizeof(*entry);
}

static int build_host_config_inb(char *hostnqn,
				 struct nvmf_host_config_entry **_entry)
{
	struct nvmf_host_config_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	strncpy(entry->hostnqn, hostnqn, MAX_NQN_SIZE);

	*_entry = entry;

	return sizeof(*entry);
}

static int build_host_delete_inb(char *hostnqn,
				 struct nvmf_host_delete_entry **_entry)
{
	struct nvmf_host_delete_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	strncpy(entry->hostnqn, hostnqn, MAX_NQN_SIZE);

	*_entry = entry;

	return sizeof(*entry);
}

static int build_link_port_inb(struct subsystem *subsys, struct portid *portid,
			    struct nvmf_link_port_entry **_entry)
{
	struct nvmf_link_port_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	strncpy(entry->subnqn, subsys->nqn, MAX_NQN_SIZE);
	entry->portid = portid->portid;

	*_entry = entry;

	return sizeof(*entry);
}

static int build_port_delete_inb(struct portid *portid,
				 struct nvmf_port_delete_entry **_entry)
{
	struct nvmf_port_delete_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	entry->portid = portid->port_num;

	*_entry = entry;

	return sizeof(*entry);
}

static int build_subsys_delete_inb(struct subsystem *subsys,
				   struct nvmf_subsys_delete_entry **_entry)
{
	struct nvmf_subsys_delete_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	strncpy(entry->subnqn, subsys->nqn, MAX_NQN_SIZE);

	*_entry = entry;

	return sizeof(*entry);
}

static int build_ns_config_inb(struct subsystem *subsys, struct ns *ns,
			       struct nvmf_ns_config_entry **_entry)
{
	struct nvmf_ns_config_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	strncpy(entry->subnqn, subsys->nqn, MAX_NQN_SIZE);
	entry->nsid = ns->nsid;

	if (ns->devid == NULLB_DEVID)
		entry->deviceid = NVMF_NULLB_DEVID;
	else
		entry->deviceid = ns->devid;

	entry->devicensid = ns->devns;

	*_entry = entry;

	return sizeof(*entry);
}

static int build_ns_delete_inb(struct subsystem *subsys, struct ns *ns,
			       struct nvmf_ns_delete_entry **_entry)
{
	struct nvmf_ns_delete_entry *entry;

	if (posix_memalign((void **) &entry, PAGE_SIZE, sizeof(*entry))) {
		print_err("no memory for buffer, errno %d", errno);
		return 0;
	}

	strncpy(entry->subnqn, subsys->nqn, MAX_NQN_SIZE);
	entry->nsid = ns->nsid;

	*_entry = entry;

	return sizeof(*entry);
}

/* out of band message formatting functions */

static inline int get_uri(struct target *target, char *uri)
{
	char			*addr = target->sc_iface.oob.address;
	int			 port = target->sc_iface.oob.port;

	return sprintf(uri, "http://%s:%d/", addr, port);
}

static void build_set_port_oob(struct portid *portid, char *buf, int len)
{
	snprintf(buf, len, "{" JSSTR "," JSSTR "," JSSTR "," JSINDX "}",
		 TAG_TYPE, portid->type, TAG_FAMILY, portid->family,
		 TAG_ADDRESS, portid->address, TAG_TRSVCID, portid->port_num);
}

static void build_set_host_oob(char *nqn, char *buf, int len)
{
	snprintf(buf, len, "{" JSSTR "}", TAG_HOSTNQN, nqn);
}

static void build_set_subsys_oob(struct subsystem *subsys, char *buf, int len)
{
	snprintf(buf, len, "{" JSSTR "," JSINDX "}",
		 TAG_SUBNQN, subsys->nqn, TAG_ALLOW_ANY, subsys->access);
}

static void build_set_ns_oob(struct ns *ns, char *buf, int len)
{
	snprintf(buf, len, "{" JSINDX "," JSINDX "," JSINDX "}",
		 TAG_NSID, ns->nsid, TAG_DEVID, ns->devid,
		 TAG_DEVNSID, ns->devns);
}

static void build_set_portid_oob(int portid, char *buf, int len)
{
	snprintf(buf, len, "{" JSINDX "}", TAG_PORTID, portid);
}

/* out of band message sending functions */

static int send_get_config_oob(struct target *target, char *tag, char **buf)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(target, uri);
	p += len;

	strcpy(p, tag);

	return exec_get(uri, buf);
}

static int send_set_portid_oob(struct target *target, char *buf, int portid)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(target, uri);
	p += len;

	sprintf(p, URI_PORTID "/%d", portid);

	return exec_post(uri, buf, strlen(buf));
}

static int send_set_config_oob(struct target *target, char *tag, char *buf)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(target, uri);
	p += len;

	strcpy(p, tag);

	return exec_post(uri, buf, strlen(buf));
}

static int send_update_subsys_oob(struct target *target, char *subsys,
				  char *tag, char *buf)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(target, uri);
	p += len;

	sprintf(p, URI_SUBSYSTEM "/%s/%s", subsys, tag);

	return exec_post(uri, buf, strlen(buf));
}

/* in band get config messages */

static inline int get_inb_nsdevs(struct target *target)
{
	struct nvmf_get_ns_devices_hdr *hdr;
	struct nvmf_get_ns_devices_entry *entry;
	struct nsdev		*nsdev;
	struct endpoint		*ep = &target->sc_iface.inb.ep;
	char			*alias = target->alias;
	int			 i;
	int			 devid;
	int			 ret;

	ret = send_get_config(ep, nvmf_get_ns_config, PAGE_SIZE,
			      (void **) &hdr);
	if (ret) {
		print_err("send get nsdevs INB failed for %s", alias);
		goto out1;
	}

	if (!hdr->num_entries) {
		print_err("No NS devices defined for %s", alias);
		goto out2;
	}

	entry = (struct nvmf_get_ns_devices_entry *) &hdr->data;

	list_for_each_entry(nsdev, &target->device_list, node)
		nsdev->valid = 0;

	for (i = hdr->num_entries; i > 0; i--, entry++) {
		devid = entry->devid;
		if (devid == NVMF_NULLB_DEVID)
			devid = NULLB_DEVID;

		list_for_each_entry(nsdev, &target->device_list, node)
			if (nsdev->nsdev == devid &&
			    nsdev->nsid == entry->nsid) {
				nsdev->valid = 1;
				goto found;
		}

		nsdev = malloc(sizeof(*nsdev));
		if (!nsdev) {
			print_err("unable to alloc nsdev");
			ret = -ENOMEM;
			goto out2;
		}

		nsdev->valid = 1;
		nsdev->nsdev = devid;
		nsdev->nsid = entry->nsid;

		set_json_inb_nsdev(target, nsdev);

		list_add_tail(&nsdev->node, &target->device_list);

		print_debug("Added %s %d:%d to %s '%s'",
			    TAG_DEVID, devid, entry->nsid, TAG_TARGET, alias);
found:
		continue;
	}

	list_for_each_entry(nsdev, &target->device_list, node)
		if (!nsdev->valid)
			print_err("Removed %s %d:%d from %s '%s'",
				  TAG_DEVID, entry->devid, entry->nsid,
				  TAG_TARGET, alias);
out2:
	free(hdr);
out1:
	return ret;
}

/* get config command handlers */

static inline int get_inb_xports(struct target *target)
{
	struct nvmf_get_transports_hdr *hdr;
	struct nvmf_get_transports_entry *entry;
	struct endpoint		*ep = &target->sc_iface.inb.ep;
	struct fabric_iface	*iface, *next;
	char			 type[CONFIG_TYPE_SIZE + 1];
	char			 fam[CONFIG_FAMILY_SIZE + 1];
	char			 addr[CONFIG_ADDRESS_SIZE + 1];
	int			 i, rdma_found;
	int			 ret;

	ret = send_get_config(ep, nvmf_get_xport_config, PAGE_SIZE,
			      (void **) &hdr);
	if (ret) {
		print_err("send get xports INB failed for %s", target->alias);
		goto out1;
	}

	if (!hdr->num_entries) {
		print_err("No transports defined for %s", target->alias);
		goto out2;
	}

	entry = (struct nvmf_get_transports_entry *) &hdr->data;

	list_for_each_entry(iface, &target->fabric_iface_list, node)
		iface->valid = 0;

	for (i = hdr->num_entries; i > 0; i--, entry++) {
		rdma_found = 0;
		memset(addr, 0, sizeof(addr));
		strcpy(type, trtype_str(entry->trtype));
		strcpy(fam, adrfam_str(entry->adrfam));
		strncpy(addr, entry->traddr, CONFIG_ADDRESS_SIZE);

		list_for_each_entry(iface, &target->fabric_iface_list, node) {
			if (strcmp(addr, iface->addr) ||
			    strcmp(fam, iface->fam))
				continue;
			if (strcmp(type, iface->type) == 0) {
				iface->valid = 1;
				if (entry->trtype == NVMF_TRTYPE_RDMA)
					rdma_found = 1;
				else
					goto found;
			}
			// TODO revisit once TCP transport is supported
			if ((entry->trtype == NVMF_TRTYPE_RDMA) &&
			    (strcmp(iface->type, TRTYPE_STR_TCP) == 0)) {
				iface->valid = 1;
				if (!rdma_found)
					rdma_found = 1;
				else
					goto found;
			}
		}

		iface = malloc(sizeof(*iface));
		if (!iface) {
			print_err("unable to alloc iface");
			ret = -ENOMEM;
			goto out2;
		}

		iface->valid = 1;
		strcpy(iface->type, type);
		strcpy(iface->fam, fam);
		strncpy(iface->addr, addr, CONFIG_ADDRESS_SIZE);

		set_json_inb_fabric_iface(target, iface);

		list_add_tail(&iface->node, &target->fabric_iface_list);

		print_debug("Added %s %s %s to %s '%s'",
			    type, fam, addr, TAG_TARGET, target->alias);
found:
		continue;
	}

	list_for_each_entry_safe(iface, next, &target->fabric_iface_list, node)
		if (!iface->valid) {
			print_err("Removed %s %s %s from %s '%s'",
				  iface->type, iface->fam, iface->addr,
				  TAG_TARGET, target->alias);

			list_del(&iface->node);
		}
out2:
	free(hdr);
out1:
	return ret;
}

static int get_inb_config(struct target *target)
{
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 ret;

	if (strcmp(ctrl->portid->type, "rdma") == 0)
		ctrl->ep.ops = rdma_register_ops();
	else
		return -EINVAL;

	ret = connect_ctrl(ctrl);
	if (ret) {
		print_err("connect_ctrl to %s returned %d", target->alias, ret);
		return ret;
	}

	ctrl->connected = 1;

	ret = get_inb_nsdevs(target);
	if (!ret)
		ret = get_inb_xports(target);

	if (ret)
		disconnect_ctrl(ctrl, 0);

	return ret;
}

/* set config (INB) command handlers */

static int _send_set_config(struct ctrl_queue *ctrl, int id, int len, void *p)
{
	int			 ret;

	if (ctrl->connected) {
		ret = send_set_config(&ctrl->ep, id, len, p);
		if (!ret)
			return 0;

		ctrl->connected = 0;
	}

	ret = connect_ctrl(ctrl);
	if (ret)
		return ret;

	ctrl->connected = 1;

	return send_set_config(&ctrl->ep, id, len, p);
}

static int config_portid_inb(struct target *target, struct portid *portid)
{
	struct nvmf_port_config_entry *entry;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_set_port_config_inb(portid, &entry);
	if (!len) {
		print_err("build set port config INB failed for %s",
			  target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_set_port_config, len, entry);
	if (ret)
		print_err("send set port INB failed for %s", target->alias);
	free(entry);

	return ret;
}

static int config_subsys_inb(struct target *target, struct subsystem *subsys)
{
	struct nvmf_subsys_config_entry *entry;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_subsys_config_inb(subsys, &entry);
	if (!len) {
		print_err("build subsys config INB failed for %s",
			  target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_set_subsys_config, len, entry);
	if (ret)
		print_err("send set subsys INB failed for %s", target->alias);
	free(entry);

	return ret;
}

/* HOST */

static int send_host_config_inb(struct target *target, struct host *host)
{
	struct nvmf_host_config_entry *entry;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_host_config_inb(host->nqn, &entry);
	if (!len) {
		print_err("build host config INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_set_host_config, len, entry);
	if (ret)
		print_err("send link host INB failed for %s", target->alias);
	free(entry);

	return ret;
}

static int send_link_host_inb(struct subsystem *subsys, struct host *host)
{
	struct nvmf_link_host_entry *entry;
	struct target		*target = subsys->target;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_link_host_inb(subsys, host, &entry);
	if (!len) {
		print_err("build link host INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_link_host_config, len, entry);
	if (ret)
		print_err("send link host INB failed for %s", target->alias);

	free(entry);

	return ret;
}

static int send_link_host_oob(struct subsystem *subsys, struct host *host)
{
	char			 uri[MAX_URI_SIZE];
	char			 buf[MAX_BODY_SIZE];
	char			*p = uri;
	int			 len;
	int			 ret;

	len = get_uri(subsys->target, p);
	p += len;

	strcpy(p, URI_HOST);

	build_set_host_oob(host->nqn, buf, sizeof(buf));

	ret = exec_post(uri, buf, strlen(buf));
	if (ret)
		return ret;

	sprintf(p, URI_SUBSYSTEM "/%s/" URI_HOST, subsys->nqn);

	return exec_post(uri, buf, strlen(buf));
}

static inline int _link_host(struct subsystem *subsys, struct host *host)
{
	struct target		*target = subsys->target;
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT) {
		ret = send_host_config_inb(target, host);
		if (!ret)
			ret = send_link_host_inb(subsys, host);
	} else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = send_link_host_oob(subsys, host);

	return ret;
}

static int send_unlink_host_inb(struct subsystem *subsys, struct host *host)
{
	struct nvmf_link_host_entry *entry;
	struct target		*target = subsys->target;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_link_host_inb(subsys, host, &entry);
	if (!len) {
		print_err("build link host INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_unlink_host_config, len, entry);
	if (ret)
		print_err("send unlink host INB failed for %s", target->alias);
	free(entry);

	return ret;
}

static int send_unlink_host_oob(struct subsystem *subsys, struct host *host)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(subsys->target, p);
	p += len;

	sprintf(p, URI_SUBSYSTEM "/%s/" URI_HOST "/%s", subsys->nqn, host->nqn);

	return exec_delete(uri);
}

static inline int _unlink_host(struct subsystem *subsys, struct host *host)
{
	struct target		*target = subsys->target;
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = send_unlink_host_inb(subsys, host);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = send_unlink_host_oob(subsys, host);

	return ret;
}

static int send_del_host_inb(struct target *target, char *hostnqn)
{
	struct nvmf_host_delete_entry *entry;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_host_delete_inb(hostnqn, &entry);
	if (!len) {
		print_err("build host delete INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_del_host_config, len, entry);
	if (ret)
		print_err("send del host INB failed for %s", target->alias);
	free(entry);

	return ret;
}

static int send_del_host_oob(struct target *target, char *hostnqn)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(target, p);
	p += len;

	sprintf(p, URI_HOST "/%s", hostnqn);

	return exec_delete(uri);
}

static inline void _del_host(struct target *target, char *hostnqn)
{
	if (target->mgmt_mode == IN_BAND_MGMT)
		send_del_host_inb(target, hostnqn);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		send_del_host_oob(target, hostnqn);
}

int update_host(char *alias, char *data, char *resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct host		*host;
	char			 newalias[MAX_ALIAS_SIZE + 1];
	char			 hostnqn[MAX_NQN_SIZE + 1];
	int			 ret;

	ret = update_json_host(alias, data, resp, newalias, hostnqn);
	if (ret)
		return ret;

	if (!alias)
		alias = newalias;

	list_for_each_entry(target, target_list, node)
		list_for_each_entry(subsys, &target->subsys_list, node)
			list_for_each_entry(host, &subsys->host_list, node)
				if (!strcmp(host->alias, alias)) {
					if (strcmp(host->nqn, hostnqn)) {
						_unlink_host(subsys, host);
						strcpy(host->nqn, hostnqn);
						_link_host(subsys, host);
					}
					if (strcmp(host->alias, newalias))
						strcpy(host->alias, newalias);
					break;
				}
	return 0;
}

int add_host(char *host, char *resp)
{
	int			 ret;

	ret = add_json_host(host, resp);

	return ret;
}

int del_host(char *alias, char *resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct host		*host;
	char			 hostnqn[MAX_NQN_SIZE + 1];
	int			 dirty;
	int			 ret;

	ret = del_json_host(alias, resp, hostnqn);
	if (ret)
		return ret;

	list_for_each_entry(target, target_list, node) {
		dirty = 0;
		list_for_each_entry(subsys, &target->subsys_list, node) {
			if (subsys->access == ALLOW_ANY)
				continue;
			dirty = 1;
			list_for_each_entry(host, &subsys->host_list, node)
				if (!strcmp(host->alias, alias)) {
					_unlink_host(subsys, host);
					list_del(&host->node);
					break;
				}
		}

		if (dirty)
			_del_host(target, hostnqn);
	}

	return 0;
}

/* link functions (hosts to subsystems and subsystems to ports */

int link_host(char *tgt, char *subnqn, char *alias, char *data, char *resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct host		*host;
	struct linked_list	 list;
	char			 newalias[MAX_ALIAS_SIZE + 1];
	char			 hostnqn[MAX_NQN_SIZE + 1];
	int			 ret;

	ret = set_json_acl(tgt, subnqn, alias, data, resp, newalias, hostnqn);
	if (ret)
		goto out;

	resp += strlen(resp);

	target = find_target(tgt);
	if (!target)
		goto out;

	subsys = find_subsys(target, subnqn);
	if (!subsys)
		goto out;

	if (!alias)
		alias = newalias;

	list_for_each_entry(host, &subsys->host_list, node)
		if (!strcmp(host->alias, alias))
			goto found;

	host = malloc(sizeof(*host));
	if (!host)
		return -ENOMEM;

	memset(host, 0, sizeof(*host));
	goto skip_unlink;
found:
	ret = _unlink_host(subsys, host);
	if (ret) {
		strcpy(resp, CONFIG_ERR);
		goto out;
	}

skip_unlink:
	strcpy(host->alias, alias);
	strcpy(host->nqn, hostnqn);

	ret = _link_host(subsys, host);
	if (ret)
		strcpy(resp, CONFIG_ERR);

	list_add_tail(&host->node, &subsys->host_list);

	create_event_host_list_for_host(&list, hostnqn);
	send_notifications(&list);
out:
	return ret;
}

int unlink_host(char *tgt, char *subnqn, char *alias, char *resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct host		*host;
	struct linked_list	 list;
	int			 ret;

	ret = del_json_acl(tgt, subnqn, alias, resp);
	if (ret)
		goto out;

	target = find_target(tgt);
	if (!target)
		goto out;

	subsys = find_subsys(target, subnqn);
	if (!subsys)
		goto out;

	list_for_each_entry(host, &subsys->host_list, node)
		if (!strcmp(host->alias, alias)) {
			_unlink_host(subsys, host);
			list_del(&host->node);
			goto found;
		}
	goto out;
found:
	list_for_each_entry(subsys, &target->subsys_list, node)
		list_for_each_entry(host, &subsys->host_list, node)
			if (!strcmp(host->alias, alias))
				goto out;

	_del_host(target, alias);

	create_event_host_list_for_host(&list, host->nqn);
	send_notifications(&list);
out:
	return ret;
}

static int send_link_portid_inb(struct subsystem *subsys, struct portid *portid)
{
	struct nvmf_link_port_entry *entry;
	struct target		*target = subsys->target;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_link_port_inb(subsys, portid, &entry);
	if (!len) {
		print_err("build link port INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_link_port_config, len, entry);
	if (ret)
		print_err("send link port INB failed for %s", target->alias);

	free(entry);

	return ret;
}

static int send_link_portid_oob(struct subsystem *subsys, struct portid *portid)
{
	struct target		*target = subsys->target;
	char			*alias = target->alias;
	char			 buf[MAX_BODY_SIZE];
	int			 ret;

	build_set_portid_oob(portid->portid, buf, sizeof(buf));

	ret = send_update_subsys_oob(target, subsys->nqn, URI_PORTID, buf);
	if (ret)
		print_err("link portid OOB failed for %s", alias);

	return ret;
}

static inline int _link_portid(struct subsystem *subsys, struct portid *portid)
{
	struct target		*target = subsys->target;
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = send_link_portid_inb(subsys, portid);
	else
		ret = send_link_portid_oob(subsys, portid);

	return ret;
}

static int send_unlink_portid_inb(struct subsystem *subsys,
				  struct portid *portid)
{
	struct nvmf_link_port_entry *entry;
	struct target		*target = subsys->target;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_link_port_inb(subsys, portid, &entry);
	if (!len) {
		print_err("build link port INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_unlink_port_config, len, entry);
	if (ret)
		print_err("send unlink port INB failed for %s", target->alias);

	free(entry);

	return ret;
}

static int send_unlink_portid_oob(struct subsystem *subsys,
				  struct portid *portid)
{
	struct target		*target = subsys->target;
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(target, p);
	p += len;

	sprintf(p, URI_SUBSYSTEM "/%s/" URI_PORTID "/%d",
		subsys->nqn, portid->portid);

	return exec_delete(uri);
}

static inline int _unlink_portid(struct subsystem *subsys,
				 struct portid *portid)
{
	struct target		*target = subsys->target;
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = send_unlink_portid_inb(subsys, portid);
	else
		ret = send_unlink_portid_oob(subsys, portid);

	return ret;
}

/* SUBSYS */

static int send_del_subsys_inb(struct subsystem *subsys)
{
	struct nvmf_subsys_delete_entry *entry;
	struct target		*target = subsys->target;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_subsys_delete_inb(subsys, &entry);
	if (!len) {
		print_err("build subsys delete INB failed for %s",
			  target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_del_subsys_config, len, entry);
	if (ret)
		print_err("send del subsys INB failed for %s", target->alias);

	free(entry);

	return ret;
}

static int send_del_subsys_oob(struct subsystem *subsys)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(subsys->target, p);
	p += len;

	sprintf(p, URI_SUBSYSTEM "/%s", subsys->nqn);

	return exec_delete(uri);
}

static inline int _del_subsys(struct subsystem *subsys)
{
	struct target		*target = subsys->target;
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = send_del_subsys_inb(subsys);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = send_del_subsys_oob(subsys);

	return ret;
}

int del_subsys(char *alias, char *nqn, char *resp)
{
	struct subsystem	*subsys;
	struct target		*target;
	struct linked_list	 list;
	int			 ret;

	ret = del_json_subsys(alias, nqn, resp);
	if (ret)
		goto out;

	target = find_target(alias);
	if (!target)
		goto out;

	subsys = find_subsys(target, nqn);
	if (!subsys)
		goto out;

	ret = _del_subsys(subsys);

	list_del(&subsys->node);

	create_event_host_list_for_subsys(&list, subsys);
	send_notifications(&list);
out:
	return ret;
}

static int config_subsys_oob(struct target *target, struct subsystem *subsys)
{
	struct ns		*ns;
	struct host		*host;
	struct portid		*portid;
	char			*alias = target->alias;
	char			*nqn = subsys->nqn;
	char			 buf[MAX_BODY_SIZE];
	int			 ret;

	build_set_subsys_oob(subsys, buf, sizeof(buf));

	ret = send_set_config_oob(target, URI_SUBSYSTEM, buf);
	if (ret) {
		print_err("set subsys OOB failed for %s", alias);
		return ret;
	}

	list_for_each_entry(ns, &subsys->ns_list, node) {
		build_set_ns_oob(ns, buf, sizeof(buf));

		ret = send_update_subsys_oob(target, nqn, URI_NAMESPACE, buf);
		if (ret)
			print_err("set subsys ns OOB failed for %s", alias);
	}

	list_for_each_entry(host, &subsys->host_list, node) {
		build_set_host_oob(host->nqn, buf, sizeof(buf));

		ret = send_set_config_oob(target, URI_HOST, buf);
		if (ret) {
			print_err("set host OOB failed for %s", alias);
			continue;
		}

		ret = send_update_subsys_oob(target, nqn, URI_HOST, buf);
		if (ret)
			print_err("set subsys acl OOB failed for %s", alias);
	}

	list_for_each_entry(portid, &target->portid_list, node)
		send_link_portid_oob(subsys, portid);

	return 0;
}

static inline int _config_subsys(struct target *target,
				 struct subsystem *subsys)
{
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = config_subsys_inb(target, subsys);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = config_subsys_oob(target, subsys);

	return ret;
}

int set_subsys(char *alias, char *nqn, char *data, char *resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct subsystem	 new_ss;
	struct portid		*portid;
	struct linked_list	 list;
	int			 len;
	int			 ret;

	memset(&new_ss, 0, sizeof(new_ss));
	new_ss.access = UNDEFINED_ACCESS;

	ret = set_json_subsys(alias, nqn, data, resp, &new_ss);
	if (ret)
		goto out;

	resp += strlen(resp);

	target = find_target(alias);
	if (!target) {
		ret = -ENOENT;
		goto out;
	}

	if (!nqn) {
		subsys = new_subsys(target, new_ss.nqn);
		if (!subsys) {
			ret = -ENOMEM;
			goto out;
		}

		subsys->access = new_ss.access;
	} else {
		subsys = find_subsys(target, nqn);
		if (!subsys) {
			ret = -ENOENT;
			goto out;
		}

		len = strlen(new_ss.nqn);
		if ((len && strcmp(nqn, new_ss.nqn)) ||
		    ((new_ss.access != UNDEFINED_ACCESS) &&
		     (new_ss.access != subsys->access))) {
			_del_subsys(subsys);

			if (len)
				strcpy(subsys->nqn, new_ss.nqn);
			if (new_ss.access != UNDEFINED_ACCESS)
				subsys->access = new_ss.access;
		}
	}

	ret = _config_subsys(target, subsys);
	if (ret) {
		strcpy(resp, CONFIG_ERR);
		goto out;
	}

	list_for_each_entry(portid, &target->portid_list, node)
		create_discovery_queue(target, subsys, portid);

	create_event_host_list_for_subsys(&list, subsys);
	send_notifications(&list);
out:
	return ret;
}

/* PORTID */

static int send_del_portid_inb(struct target *target, struct portid *portid)
{
	struct nvmf_port_delete_entry *entry;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_port_delete_inb(portid, &entry);
	if (!len) {
		print_err("build port delete INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_del_port_config, len, entry);
	if (ret)
		print_err("send del port INB failed for %s", target->alias);

	free(entry);

	return ret;
}

static int send_del_portid_oob(struct target *target, struct portid *portid)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(target, p);
	p += len;

	sprintf(p, URI_PORTID "/%d", portid->portid);

	return exec_delete(uri);
}

static inline int _del_portid(struct target *target, struct portid *portid)
{
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = send_del_portid_inb(target, portid);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = send_del_portid_oob(target, portid);

	return ret;
}

int del_portid(char *alias, int id, char *resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct portid		*portid;
	struct ctrl_queue	*dq, *next_dq;
	struct logpage		*logpage, *next_log;
	int			 ret;

	ret = del_json_portid(alias, id, resp);
	if (ret)
		goto out;

	target = find_target(alias);
	if (!target)
		goto out;

	portid = find_portid(target, id);
	if (!portid)
		goto out;

	list_for_each_entry_safe(dq, next_dq,
				 &target->discovery_queue_list, node) {
		if (dq->portid != portid)
			continue;
		if (dq->connected)
			disconnect_ctrl(dq, 0);
		list_del(&dq->node);
		free(dq);
	}

	list_for_each_entry(subsys, &target->subsys_list, node)
		list_for_each_entry_safe(logpage, next_log,
					 &subsys->logpage_list, node) {
			if (logpage->portid != portid)
				continue;
			list_del(&logpage->node);
			free(logpage);
		}

	list_del(&portid->node);
	free(portid);
out:
	return ret;
}

static int config_portid_oob(struct target *target, struct portid *portid)
{
	int			 ret;
	char			 buf[MAX_BODY_SIZE];

	build_set_port_oob(portid, buf, sizeof(buf));

	ret = send_set_portid_oob(target, buf, portid->portid);
	if (ret)
		print_err("set port OOB failed for %s", target->alias);

	return ret;
}

static inline int _config_portid(struct target *target, struct portid *portid)
{
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = config_portid_inb(target, portid);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = config_portid_oob(target, portid);

	return ret;
}

int set_portid(char *alias, int id, char *data, char *resp)
{
	int			 ret;
	struct portid		*portid;
	struct portid		 delportid;
	struct target		*target;
	struct subsystem	*subsys;

	portid = malloc(sizeof(*portid));
	if (!portid)
		return -ENOMEM;

	memset(portid, 0, sizeof(*portid));

	ret = set_json_portid(alias, id, data, resp, portid);
	if (ret) {
		free(portid);
		goto out;
	}

	resp += strlen(resp);

	target = find_target(alias);
	if (!target) {
		free(portid);
		ret = -ENOENT;
		strcpy(resp, TARGET_ERR);
		goto out;
	}

	list_add_tail(&portid->node, &target->portid_list);

	if (target->mgmt_mode == LOCAL_MGMT) {
		create_discovery_queue(target, NULL, portid);
		return 0;
	}

	delportid.portid = id;
	list_for_each_entry(subsys, &target->subsys_list, node)
		_unlink_portid(subsys, &delportid);

	if ((portid->portid != id) && id)
		_del_portid(target, &delportid);

	ret = _config_portid(target, portid);
	if (ret) {
		strcpy(resp, CONFIG_ERR);
		goto out;
	}

	create_discovery_queue(target, NULL, portid);

	list_for_each_entry(subsys, &target->subsys_list, node) {
		_link_portid(subsys, portid);
		create_discovery_queue(target, subsys, portid);
	}
out:
	return ret;
}

/* NAMESPACE */

static int send_set_ns_inb(struct subsystem *subsys, struct ns *ns)
{
	struct nvmf_ns_config_entry *entry;
	struct target		*target = subsys->target;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_ns_config_inb(subsys, ns, &entry);
	if (!len) {
		print_err("build ns config INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_set_ns_config, len, entry);
	if (ret)
		print_err("send set ns config INB failed for %s",
			  target->alias);
	free(entry);

	return ret;
}

static int send_set_ns_oob(struct subsystem *subsys, struct ns *ns)
{
	char			 uri[MAX_URI_SIZE];
	char			 buf[MAX_BODY_SIZE];
	char			*p = uri;
	int			 len;

	build_set_ns_oob(ns, buf, sizeof(buf));

	len = get_uri(subsys->target, p);
	p += len;

	sprintf(p, URI_SUBSYSTEM "/%s/" URI_NAMESPACE "/%d",
		subsys->nqn, ns->nsid);

	return exec_post(uri, buf, strlen(buf));
}

static inline int _set_ns(struct subsystem *subsys, struct ns *ns)
{
	int			 ret = 0;
	struct target		*target = subsys->target;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = send_set_ns_inb(subsys, ns);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = send_set_ns_oob(subsys, ns);

	return ret;
}

int set_ns(char *alias, char *nqn, char *data, char *resp)
{
	struct subsystem	*subsys;
	struct target		*target;
	struct nsdev		*nsdev;
	struct ns		*ns;
	struct ns		 result;
	int			 ret;

	memset(&ns, 0, sizeof(ns));

	ret = set_json_ns(alias, nqn, data, resp, &result);
	if (ret)
		goto out;

	resp += strlen(resp);

	target = find_target(alias);
	if (!target)
		goto out;

	if (target->mgmt_mode == LOCAL_MGMT)
		goto found;

	if (result.devid == NULLB_DEVID)
		result.devns = 0;

	list_for_each_entry(nsdev, &target->device_list, node)
		if ((nsdev->nsdev == result.devid) &&
		    (nsdev->nsid == result.devns))
			goto found;

	strcpy(resp, NSDEV_ERR);

	ret = -ENOENT;
	goto out;
found:
	subsys = find_subsys(target, nqn);
	if (!subsys)
		goto out;

	ns = find_ns(subsys, result.nsid);
	if (!ns) {
		ns = malloc(sizeof(*ns));
		if (!ns) {
			strcpy(resp, INTERNAL_ERR);
			ret = -ENOMEM;
			goto out;
		}
		ns->nsid = result.nsid;

		list_add_tail(&ns->node, &subsys->ns_list);
	}

	ns->devid = result.devid;
	ns->devns = result.devns;

	ret = _set_ns(subsys, ns);
	if (ret)
		strcpy(resp, CONFIG_ERR);
out:
	return ret;
}

static int send_del_ns_inb(struct subsystem *subsys, struct ns *ns)
{
	struct nvmf_ns_delete_entry *entry;
	struct target		*target = subsys->target;
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 len;
	int			 ret;

	len = build_ns_delete_inb(subsys, ns, &entry);
	if (!len) {
		print_err("build ns delete INB failed for %s", target->alias);
		return -ENOMEM;
	}

	ret = _send_set_config(ctrl, nvmf_del_ns_config, len, entry);
	if (ret)
		print_err("send del ns config INB failed for %s",
			  target->alias);
	free(entry);

	return ret;
}

static int send_del_ns_oob(struct subsystem *subsys, struct ns *ns)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(subsys->target, p);
	p += len;

	sprintf(p, URI_SUBSYSTEM "/%s/" URI_NAMESPACE "/%d",
		subsys->nqn, ns->nsid);

	return exec_delete(uri);
}

int del_ns(char *alias, char *nqn, int nsid, char *resp)
{
	struct subsystem	*subsys;
	struct target		*target;
	struct ns		*ns;
	int			 ret;

	ret = del_json_ns(alias, nqn, nsid, resp);
	if (ret)
		goto out;

	target = find_target(alias);
	if (!target)
		goto out;

	subsys = find_subsys(target, nqn);
	if (!subsys)
		goto out;

	ns = find_ns(subsys, nsid);
	if (!ns)
		goto out;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = send_del_ns_inb(subsys, ns);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = send_del_ns_oob(subsys, ns);

	list_del(&ns->node);
out:
	return ret;
}

/* TARGET */

static int get_oob_nsdevs(struct target *target)
{
	char			*data;
	char			*alias = target->alias;
	int			 ret;

	ret = send_get_config_oob(target, URI_NSDEV, &data);
	if (ret) {
		print_err("send get nsdevs OOB failed for %s", alias);
		goto out1;
	}

	ret = set_json_oob_nsdevs(target, data);
	if (ret)
		print_err("send get nsdevs OOB failed for %s", alias);

	free(data);
out1:
	return ret;
}

static int get_oob_xports(struct target *target)
{
	char			*data;
	int			 ret;

	ret = send_get_config_oob(target, URI_INTERFACE, &data);
	if (ret)
		return ret;

	ret = set_json_oob_interfaces(target, data);

	free(data);

	return ret;
}

static int get_oob_config(struct target *target)
{
	int			 ret;

	ret = get_oob_nsdevs(target);
	if (ret)
		return ret;

	return get_oob_xports(target);
}

static int config_target_inb(struct target *target)
{
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	struct portid		*portid;
	struct subsystem	*subsys;
	struct ns		*ns;
	struct host		*host;
	int			 ret;

	if (!ctrl->connected) {
		ret = connect_ctrl(ctrl);
		if (ret) {
			print_err("connect_ctrl to %s returned %d",
				  target->alias, ret);
			goto out1;
		}
	}

	list_for_each_entry(portid, &target->portid_list, node) {
		ret = config_portid_inb(target, portid);
		if (ret)
			goto out2;
	}

	list_for_each_entry(subsys, &target->subsys_list, node) {
		ret = config_subsys_inb(target, subsys);
		if (ret)
			goto out2;

		if (!subsys->access)
			list_for_each_entry(host, &subsys->host_list, node) {
				ret = send_host_config_inb(target, host);
				if (ret)
					goto out2;

				ret = send_link_host_inb(subsys, host);
				if (ret)
					goto out2;
			}

		list_for_each_entry(ns, &subsys->ns_list, node) {
			ret = send_set_ns_inb(subsys, ns);
			if (ret)
				goto out2;
		}

		list_for_each_entry(portid, &target->portid_list, node) {
			ret = send_link_portid_inb(subsys, portid);
			if (ret)
				goto out2;
		}
	}

	return 0;
out2:
	disconnect_ctrl(ctrl, 0);
out1:
	return ret;
}

static int config_target_oob(struct target *target)
{
	struct portid		*portid;
	struct subsystem	*subsys;
	int			 ret = 0;

	list_for_each_entry(portid, &target->portid_list, node) {
		ret = config_portid_oob(target, portid);
		if (ret)
			goto out;
	}

	list_for_each_entry(subsys, &target->subsys_list, node) {
		ret = config_subsys_oob(target, subsys);
		if (ret)
			break;
	}
out:
	return ret;
}

int get_config(struct target *target)
{
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = get_inb_config(target);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = get_oob_config(target);

	return ret;
}

int config_target(struct target *target)
{
	int			 ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		ret = config_target_inb(target);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		ret = config_target_oob(target);

	return ret;
}

static int _send_reset_config(struct ctrl_queue *ctrl)
{
	int			 ret;

	if (ctrl->connected) {
		ret = send_reset_config(&ctrl->ep);
		if (!ret)
			return 0;

		ctrl->connected = 0;
	}

	ret = connect_ctrl(ctrl);
	if (ret)
		return ret;

	ctrl->connected = 1;

	return send_reset_config(&ctrl->ep);
}

static int send_del_target_inb(struct target *target)
{
	struct ctrl_queue	*ctrl = &target->sc_iface.inb;
	int			 ret;

	ret = _send_reset_config(ctrl);
	if (ret)
		print_err("send reset config INB failed for %s", target->alias);

	return ret;
}

static int send_del_target_oob(struct target *target)
{
	char			 uri[MAX_URI_SIZE];
	char			*p = uri;
	int			 len;

	len = get_uri(target, uri);
	p += len;

	strcpy(p, URI_CONFIG);

	return exec_delete(uri);
}

int send_del_target(struct target *target)
{
	int			ret = 0;

	if (target->mgmt_mode == IN_BAND_MGMT)
		send_del_target_inb(target);
	else if (target->mgmt_mode == OUT_OF_BAND_MGMT)
		send_del_target_oob(target);

	return ret;
}

int del_target(char *alias, char *resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct portid		*portid;
	struct host		*host;
	struct linked_list	 list;

	int			 ret;

	ret = del_json_target(alias, resp);
	if (ret)
		goto out;

	target = find_target(alias);
	if (!target)
		goto out;

	list_for_each_entry(subsys, &target->subsys_list, node) {
		_del_subsys(subsys);

		list_for_each_entry(host, &subsys->host_list, node)
			_del_host(target, host->alias);
	}

	list_for_each_entry(portid, &target->portid_list, node)
		_del_portid(target, portid);

	list_del(&target->node);

	create_event_host_list_for_target(&list, target);
	send_notifications(&list);
out:
	return ret;
}

static inline void set_oob_interface(union sc_iface *iface,
				     union sc_iface *result)
{
	if (iface->inb.portid) {
		free(iface->inb.portid);
		iface->inb.portid = NULL;
	}

	iface->oob.port = result->oob.port;
	strcpy(iface->oob.address, result->oob.address);
}

static inline void set_inb_interface(union sc_iface *iface,
				     union sc_iface *result)
{
	struct portid		*portid = iface->inb.portid;

	if (!iface->inb.portid) {
		portid = malloc(sizeof(*portid));
		if (!portid)
			return;
		iface->inb.portid = portid;
	}

	strcpy(portid->port, result->inb.portid->port);
	strcpy(portid->type, result->inb.portid->type);
	strcpy(portid->address, result->inb.portid->address);
}

int set_interface(char *alias, char *data, char *resp)
{
	int			 ret;
	struct target		*target;
	union sc_iface		*iface;
	union sc_iface		 result;
	int			 mode;

	target = find_target(alias);
	if (!target) {
		ret = -ENOENT;
		strcpy(resp, TARGET_ERR);
		goto out;
	}

	mode = target->mgmt_mode;

	if (mode == OUT_OF_BAND_MGMT)
		ret = set_json_oob_interface(alias, data, resp, &result);
	else if (mode == IN_BAND_MGMT)
		ret = set_json_inb_interface(alias, data, resp, &result);
	else {
		strcpy(resp, MGMT_MODE_ERR);
		ret = -EINVAL;
	}

	if (ret)
		goto out;

	resp += strlen(resp);

	iface = &target->sc_iface;

	if (mode == OUT_OF_BAND_MGMT)
		set_oob_interface(iface, &result);
	else if (mode == IN_BAND_MGMT)
		set_inb_interface(iface, &result);
out:
	return ret;
}

int add_target(char *alias, char *resp)
{
	struct target		*target;
	int			 ret;

	ret = add_json_target(alias, resp);
	if (ret) {
		print_err("Target %s already exists", alias);
		return -EEXIST;
	}

	target = alloc_target(alias);
	if (!target)
		return -ENOMEM;

	return 0;
}

int update_target(char *alias, char *data, char *resp)
{
	struct target		 result;
	struct target		*target;
	struct portid		 portid;
	struct linked_list	 list;
	int			 ret;

	memset(&result, 0, sizeof(result));
	memset(&portid, 0, sizeof(portid));

	result.sc_iface.inb.portid = &portid;

	ret = update_json_target(alias, data, resp, &result);
	if (ret)
		return ret;

	if (!alias) {
		target = alloc_target(result.alias);
		if (!target)
			return -ENOMEM;
	} else {
		target = find_target(alias);
		if (unlikely(!target))
			return -EFAULT;

		if (strcmp(result.alias, alias))
			strcpy(target->alias, result.alias);
	}

	target->mgmt_mode = result.mgmt_mode;
	target->refresh	  = result.refresh;

	if (target->mgmt_mode == OUT_OF_BAND_MGMT) {
		set_oob_interface(&target->sc_iface, &result.sc_iface);
		get_oob_config(target);
	} else if (target->mgmt_mode == IN_BAND_MGMT) {
		set_inb_interface(&target->sc_iface, &result.sc_iface);
		get_inb_config(target);
	}

	create_event_host_list_for_target(&list, target);
	send_notifications(&list);

	return ret;
}

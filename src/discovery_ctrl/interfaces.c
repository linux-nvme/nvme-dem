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
#include <dirent.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "common.h"

int target_reconfig(char *alias)
{
	struct target		*target;
	int			 ret;

	list_for_each_entry(target, target_list, node)
		if (!strcmp(target->alias, alias))
			goto found;

	return -ENOENT;
found:
	ret = send_del_target(target);
	if (ret)
		return ret;

	return config_target(target);
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
				 struct ctrl_queue *dq)
{
	logpage->e = *e;
	logpage->valid = 1;
	logpage->portid = dq->portid;
}

static void save_log_pages(struct nvmf_disc_rsp_page_hdr *log, int numrec,
			   struct target *target, struct ctrl_queue *dq)
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

				logpage = malloc(sizeof(*logpage));
				if (!logpage) {
					print_err("alloc new logpage failed");
					return;
				}

				store_logpage(logpage, e, dq);

				list_add_tail(&logpage->node,
					      &subsys->logpage_list);
next:
				break;
			}

			if (!found)
				print_err("unknown subsystem %s on target %s",
					  e->subnqn, target->alias);
	}
}

void fetch_log_pages(struct ctrl_queue *dq)
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

int target_refresh(char *alias)
{
	struct target		*target;
	struct ctrl_queue	*dq;

	list_for_each_entry(target, target_list, node)
		if (!strcmp(target->alias, alias))
			goto found;

	return -ENOENT;
found:
	list_for_each_entry(dq, &target->discovery_queue_list, node) {
		if (!connect_ctrl(dq)) {
			fetch_log_pages(dq);
			disconnect_ctrl(dq, 0);
		}
	}

	return 0;
}

int target_usage(char *alias, char **results)
{
	struct target		*target;

	list_for_each_entry(target, target_list, node)
		if (!strcmp(target->alias, alias))
			goto found;

	return -ENOENT;
found:
	sprintf(*results, "TODO return Target Usage info");
	return 0;
}

static void format_logpage(char *buf, struct nvmf_disc_rsp_page_entry *e)
{
	int			 n;
	char			*p = buf;

	n = sprintf(p, "<p>trtype <b>%s</b> ", trtype_str(e->trtype));
	p += n;
	n = sprintf(p, "adrfam <b>%s</b> ", adrfam_str(e->adrfam));
	p += n;
	n = sprintf(p, "subtype <b>%s</b> ", subtype_str(e->subtype));
	p += n;
	n = sprintf(p, "treq <b>%s</b> ", treq_str(e->treq));
	p += n;
	n = sprintf(p, "portid <b>%d</b> ", e->portid);
	p += n;
	n = sprintf(p, "trsvcid <b>%s</b> ", e->trsvcid);
	p += n;
	n = sprintf(p, "subnqn <b>%s</b> ", e->subnqn);
	p += n;
	n = sprintf(p, "traddr <b>%s</b><br>", e->traddr);
	p += n;

	switch (e->trtype) {
	case NVMF_TRTYPE_RDMA:
		n = sprintf(p, " &nbsp; rdma: ");
		p += n;
		n = sprintf(p, "prtype <b>%s</b> ",
			    prtype_str(e->tsas.rdma.prtype));
		p += n;
		n = sprintf(p, "qptype <b>%s</b> ",
			    qptype_str(e->tsas.rdma.qptype));
		p += n;
		n = sprintf(p, "cms <b>%s</b> ", cms_str(e->tsas.rdma.cms));
		p += n;
		n = sprintf(p, "pkey <b>0x%04x</b>", e->tsas.rdma.pkey);
		p += n;
		break;
	}
	n = sprintf(p, "</p>");
	p += n;
}

int target_logpage(char *alias, char **resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct logpage		*logpage;
	char			 buf[MAX_BODY_SIZE + 1];
	char			*p = *resp;
	u64			 n, len = 0;
	u64			 bytes = MAX_BODY_SIZE;

	list_for_each_entry(target, target_list, node)
		if (!strcmp(target->alias, alias))
			goto found;

	return -ENOENT;
found:
	list_for_each_entry(subsys, &target->subsys_list, node)
		list_for_each_entry(logpage, &subsys->logpage_list, node) {
			if (!logpage->valid)
				continue;

			format_logpage(buf, &logpage->e);

			if ((len + strlen(buf)) > bytes) {
				bytes += MAX_BODY_SIZE;
				p = realloc(*resp, bytes);
				if (!p)
					return -ENOMEM;
				*resp = p;
				p += len;
			}
			strcpy(p, buf);
			n = strlen(buf);
			p += n;
			len += n;
		}

	if (!len)
		sprintf(*resp, "No valid Log Pages");

	return 0;
}

int host_logpage(char *alias, char **resp)
{
	struct host		*host;
	struct target		*target;
	struct subsystem	*subsys;
	struct logpage		*logpage;
	char			 buf[MAX_BODY_SIZE + 1];
	char			*p = *resp;
	u64			 n, len = 0;
	u64			 bytes = MAX_BODY_SIZE;

	list_for_each_entry(target, target_list, node) {
		if (target->group_member &&
		    !indirect_shared_group(target, alias))
			continue;

		list_for_each_entry(subsys, &target->subsys_list, node) {
			if (subsys->access == ALLOW_ANY)
				goto found;

			list_for_each_entry(host, &subsys->host_list, node)
				if (!strcmp(alias, host->alias))
					goto found;
			continue;
found:
			list_for_each_entry(logpage, &subsys->logpage_list,
					    node) {
				if (!logpage->valid)
					continue;

				format_logpage(buf, &logpage->e);

				if ((len + strlen(buf)) > bytes) {
					bytes += MAX_BODY_SIZE;
					p = realloc(*resp, bytes);
					if (!p)
						return -ENOMEM;
					*resp = p;
					p += len;
				}
				strcpy(p, buf);
				n = strlen(buf);
				p += n;
				len += n;
			}
		}
	}

	if (!len)
		sprintf(*resp, "No valid Log Pages");

	return 0;
}

static void check_host(struct subsystem *subsys, json_t *acl,
		       const char *alias, const char *nqn)
{
	json_t			*obj;
	struct host		*host;
	int			 i, n;

	n = json_array_size(acl);
	for (i = 0; i < n; i++) {
		obj = json_array_get(acl, i);
		if (obj && !strcmp(alias, json_string_value(obj))) {
			host = malloc(sizeof(*host));
			if (!host)
				return;

			memset(host, 0, sizeof(*host));

			host->subsystem = subsys;
			strcpy(host->nqn, nqn);
			strcpy(host->alias, alias);

			list_add_tail(&host->node, &subsys->host_list);
		}
	}
}

static void check_namespaces(struct subsystem *subsys, json_t *list)
{
	json_t			*iter;
	json_t			*obj;
	struct ns		*ns;
	int			 i, n;
	int			 nsid, devid, devns;

	n = json_array_size(list);
	for (i = 0; i < n; i++) {
		iter = json_array_get(list, i);
		obj = json_object_get(iter, TAG_NSID);
		if (unlikely(!obj) || !json_is_integer(obj))
			continue;

		nsid = json_integer_value(obj);

		obj = json_object_get(iter, TAG_DEVID);
		if (unlikely(!obj) || !json_is_integer(obj))
			continue;

		devid = json_integer_value(obj);

		obj = json_object_get(iter, TAG_DEVNSID);
		if (devid == NULL_BLK_DEVID || !obj || !json_is_integer(obj))
			devns = 0;
		else
			devns = json_integer_value(obj);

		ns = malloc(sizeof(*ns));
		if (!ns)
			return;

		memset(ns, 0, sizeof(*ns));

		ns->nsid	= nsid;
		ns->devid	= devid;
		ns->devns	= devns;
		// TODO add bits for multipath and partitions

		list_add_tail(&ns->node, &subsys->ns_list);
	}
}

static void check_hosts(struct subsystem *subsys, json_t *acl, json_t *hosts)
{
	json_t			*iter;
	json_t			*obj;
	const char		*alias;
	int			 i, n;

	n = json_array_size(hosts);
	for (i = 0; i < n; i++) {
		iter = json_array_get(hosts, i);

		obj = json_object_get(iter, TAG_ALIAS);
		if (unlikely(!obj))
			continue;

		alias = json_string_value(obj);

		obj = json_object_get(iter, TAG_HOSTNQN);
		if (unlikely(!obj))
			continue;

		check_host(subsys, acl, alias, json_string_value(obj));
	}
}

struct subsystem *new_subsys(struct target *target, char *nqn)
{
	struct subsystem	*subsys;

	subsys = malloc(sizeof(*subsys));
	if (!subsys)
		return NULL;

	memset(subsys, 0, sizeof(*subsys));
	subsys->target = target;
	strcpy(subsys->nqn, nqn);

	INIT_LINKED_LIST(&subsys->host_list);
	INIT_LINKED_LIST(&subsys->ns_list);
	INIT_LINKED_LIST(&subsys->logpage_list);

	list_add_tail(&subsys->node, &target->subsys_list);

	return subsys;
}

static void check_subsystems(struct target *target, json_t *array,
			     json_t *hosts)
{
	json_t			*obj;
	json_t			*iter;
	json_t			*nqn;
	struct subsystem	*subsys;
	int			 i, n;

	n = json_array_size(array);
	for (i = 0; i < n; i++) {
		iter = json_array_get(array, i);
		nqn = json_object_get(iter, TAG_SUBNQN);
		if (!nqn)
			continue;

		subsys = new_subsys(target, (char *) json_string_value(nqn));
		if (!subsys)
			return;

		obj = json_object_get(iter, TAG_NSIDS);
		if (obj)
			check_namespaces(subsys, obj);

		obj = json_object_get(iter, TAG_ALLOW_ANY);
		if (obj && json_is_integer(obj))
			subsys->access = json_integer_value(obj);

		obj = json_object_get(iter, TAG_HOSTS);

		if (!subsys->access && obj && hosts)
			check_hosts(subsys, obj, hosts);
	}
}

static int get_transport_info(char *alias, json_t *grp, struct portid *portid)
{
	json_t			*obj;
	char			*str;
	int			 addr[ADDR_LEN];
	int			 ret;

	obj = json_object_get(grp, TAG_TYPE);
	if (!obj) {
		print_err("Controller '%s' error: transport type missing",
			  alias);
		goto out;
	}
	str = (char *) json_string_value(obj);
	strncpy(portid->type, str, CONFIG_TYPE_SIZE);

	obj = json_object_get(grp, TAG_PORTID);
	portid->portid = json_integer_value(obj);

	obj = json_object_get(grp, TAG_FAMILY);
	if (!obj) {
		print_err("Controller '%s' error: transport family missing",
			  alias);
		goto out;
	}
	str = (char *) json_string_value(obj);
	strncpy(portid->family, str, CONFIG_FAMILY_SIZE);

	obj = json_object_get(grp, TAG_ADDRESS);
	if (!obj) {
		print_err("Controller '%s' error: transport address missing",
			  alias);
		goto out;
	}
	str = (char *) json_string_value(obj);

	if (strcmp(portid->family, "ipv4") == 0)
		ret = ipv4_to_addr(str, addr);
	else if (strcmp(portid->family, "ipv6") == 0)
		ret = ipv6_to_addr(str, addr);
	else if (strcmp(portid->family, "fc") == 0)
		ret = fc_to_addr(str, addr);
	else {
		print_err("Controller '%s' error: bad transport family '%s'",
			  alias, portid->family);
		goto out;
	}

	if (ret < 0) {
		print_err("Controller '%s' error: bad '%s' address '%s'",
			  alias, portid->family, str);
		goto out;
	}

	memcpy(portid->addr, addr, sizeof(addr[0]) * ADDR_LEN);
	strncpy(portid->address, str, CONFIG_ADDRESS_SIZE);

	obj = json_object_get(grp, TAG_TRSVCID);
	if (obj)
		portid->port_num = json_integer_value(obj);
	else
		portid->port_num = NVME_RDMA_IP_PORT;

	snprintf(portid->port, CONFIG_PORT_SIZE, "%d", portid->port_num);

	return 1;
out:
	return 0;
}

struct target *alloc_target(char *alias)
{
	struct target	*target;

	target = malloc(sizeof(*target));
	if (!target)
		return NULL;

	memset(target, 0, sizeof(*target));

	INIT_LINKED_LIST(&target->subsys_list);
	INIT_LINKED_LIST(&target->portid_list);
	INIT_LINKED_LIST(&target->fabric_iface_list);
	INIT_LINKED_LIST(&target->device_list);
	INIT_LINKED_LIST(&target->discovery_queue_list);

	list_add_tail(&target->node, target_list);

	strncpy(target->alias, alias, MAX_ALIAS_SIZE);

	return target;
}

static int setup_oob_target(json_t *parent, struct target *target)
{
	json_t			*iface;
	json_t			*obj;
	int			 ret = 1;

	iface = json_object_get(parent, TAG_INTERFACE);
	if (!iface)
		goto out;

	obj = json_object_get(iface, TAG_IFADDRESS);
	if (!obj || !json_is_string(obj))
		goto out;

	strcpy(target->sc_iface.oob.address,
	       (char *) json_string_value(obj));

	obj = json_object_get(iface, TAG_IFPORT);
	if (!obj || !json_is_integer(obj))
		goto out;

	target->sc_iface.oob.port = json_integer_value(obj);

	ret = 0;
out:
	return ret;
}

static int setup_inb_target(json_t *parent, struct target *target)
{
	json_t			*iface;
	json_t			*obj;

	target->sc_iface.inb.target = target;
	strcpy(target->sc_iface.inb.hostnqn, DISCOVERY_CTRL_NQN);

	target->sc_iface.inb.portid = malloc(sizeof(struct portid));
	if (!target->sc_iface.inb.portid)
		return -ENOMEM;

	iface = json_object_get(parent, TAG_INTERFACE);
	if (!iface)
		goto reset;

	obj = json_object_get(iface, TAG_TYPE);
	if (!obj || !json_is_string(obj))
		goto reset;

	strcpy(target->sc_iface.inb.portid->type,
	       (char *) json_string_value(obj));

	obj = json_object_get(iface, TAG_FAMILY);
	if (!obj || !json_is_string(obj))
		goto reset;

	strcpy(target->sc_iface.inb.portid->family,
	       (char *) json_string_value(obj));

	obj = json_object_get(iface, TAG_ADDRESS);
	if (!obj || !json_is_string(obj))
		goto reset;

	strcpy(target->sc_iface.inb.portid->address,
	       (char *) json_string_value(obj));

	obj = json_object_get(iface, TAG_TRSVCID);
	if (!obj || !json_is_integer(obj))
		goto reset;

	target->sc_iface.inb.portid->port_num = json_integer_value(obj);

	return 0;
reset:
	print_err("In-Band SC misconfigured %s, reverting to Local Management",
		  target->alias);

	target->mgmt_mode = LOCAL_MGMT;
	free(target->sc_iface.inb.portid);

	return 0;
}

static struct target *add_to_target_list(json_t *parent, json_t *hosts)
{
	struct target		*target;
	json_t			*subgroup;
	json_t			*obj;
	char			 alias[MAX_ALIAS_SIZE + 1];
	int			 refresh = 0;
	int			 mgmt_mode = LOCAL_MGMT;

	obj = json_object_get(parent, TAG_ALIAS);
	if (!obj)
		goto err;

	strncpy(alias, (char *) json_string_value(obj), MAX_ALIAS_SIZE);

	target = alloc_target(alias);
	if (!target)
		goto err;

	obj = json_object_get(parent, TAG_REFRESH);
	if (obj && json_is_integer(obj))
		refresh = json_integer_value(obj);

	obj = json_object_get(parent, TAG_MGMT_MODE);
	if (obj && json_is_string(obj))
		mgmt_mode = get_mgmt_mode((char *) json_string_value(obj));

	target->mgmt_mode = mgmt_mode;

	if (mgmt_mode == OUT_OF_BAND_MGMT) {
		if (setup_oob_target(parent, target))
			goto err;
	} else if (mgmt_mode == IN_BAND_MGMT) {
		if (setup_inb_target(parent, target))
			goto err;
	}

	target->json = parent;
	target->refresh = refresh;

	subgroup = json_object_get(parent, TAG_SUBSYSTEMS);
	if (subgroup)
		check_subsystems(target, subgroup, hosts);

	return target;
err:
	return NULL;
}

static int add_port_to_target(struct target *target, json_t *obj)
{
	struct portid		*portid;

	portid = malloc(sizeof(*portid));
	if (!portid)
		return -ENOMEM;

	memset(portid, 0, sizeof(*portid));

	list_add_tail(&portid->node, &target->portid_list);

	if (!get_transport_info(target->alias, obj, portid))
		goto err;

	return 0;
err:
	free(portid);

	return -EINVAL;
}

static void get_address_str(const struct sockaddr *sa, char *s, size_t len)
{
	switch (sa->sa_family) {
	case AF_INET:
		inet_ntop(AF_INET, &(((struct sockaddr_in *) sa)->sin_addr),
			  s, len);
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) sa)->sin6_addr),
			  s, len);
		break;
	default:
		strncpy(s, "Unknown AF", len);
	}
}

static void build_group_list(void)
{
	struct json_context	*ctx = get_json_context();
	struct group		*group;
	json_t			*array;
	json_t			*iter;
	json_t			*links;
	json_t			*obj;
	int			 i, j, num_groups, num_links;

	array = json_object_get(ctx->root, TAG_GROUPS);
	if (!array)
		return;
	num_groups = json_array_size(array);
	if (!num_groups)
		return;

	for (i = 0; i < num_groups; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NAME);
		group = init_group((char *) json_string_value(obj));

		links = json_object_get(iter, TAG_TARGETS);
		num_links = json_array_size(links);
		for (j = 0; j < num_links; j++) {
			obj = json_array_get(links, j);
			add_target_to_group(group,
					    (char *) json_string_value(obj));
		}

		links = json_object_get(iter, TAG_HOSTS);
		num_links = json_array_size(links);
		for (j = 0; j < num_links; j++) {
			obj = json_array_get(links, j);
			add_host_to_group(group,
					  (char *) json_string_value(obj));
		}
	}
}

static void build_target_list(void)
{
	struct json_context	*ctx = get_json_context();
	struct target		*target;
	json_t			*array;
	json_t			*hosts;
	json_t			*ports;
	json_t			*iter;
	json_t			*obj;
	int			 i, j, num_targets, num_ports;

	array = json_object_get(ctx->root, TAG_TARGETS);
	if (!array)
		return;
	num_targets = json_array_size(array);
	if (!num_targets)
		return;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	for (i = 0; i < num_targets; i++) {
		iter = json_array_get(array, i);
		target = add_to_target_list(iter, hosts);

		ports = json_object_get(iter, TAG_PORTIDS);
		if (ports) {
			num_ports = json_array_size(ports);
			for (j = 0; j < num_ports; j++) {
				obj = json_array_get(ports, j);
				add_port_to_target(target, obj);
			}
		}
	}
}

void build_lists(void)
{
	build_target_list();
	build_group_list();
}

static int count_dem_config_files(void)
{
	struct dirent		*entry;
	DIR			*dir;
	int			 filecount = 0;

	dir = opendir(PATH_NVMF_DEM_DISC);
	if (dir != NULL) {
		for_each_dir(entry, dir)
			if ((strcmp(entry->d_name, CONFIG_FILENAME)) &&
			    (strcmp(entry->d_name, SIGNATURE_FILE_FILENAME)))
				filecount++;
		closedir(dir);
	} else {
		print_err("%s does not exist", PATH_NVMF_DEM_DISC);
		filecount = -ENOENT;
	}

	return filecount;
}

static void read_dem_config(FILE *fid, struct host_iface *iface)
{
	char			 tag[LARGEST_TAG + 1];
	char			 val[LARGEST_VAL + 1];
	int			 ret;

	ret = parse_line(fid, tag, LARGEST_TAG, val, LARGEST_VAL);
	if (ret)
		return;

	if (strcasecmp(tag, TAG_TYPE) == 0)
		strncpy(iface->type, val, CONFIG_TYPE_SIZE);
	else if (strcasecmp(tag, TAG_FAMILY) == 0)
		strncpy(iface->family, val, CONFIG_FAMILY_SIZE);
	else if (strcasecmp(tag, TAG_ADDRESS) == 0)
		strncpy(iface->address, val, CONFIG_ADDRESS_SIZE);
	else if (strcasecmp(tag, TAG_TRSVCID) == 0)
		strncpy(iface->port, val, CONFIG_PORT_SIZE);
}

static void translate_addr_to_array(struct host_iface *iface)
{
	if (strcmp(iface->family, "ipv4") == 0)
		ipv4_to_addr(iface->address, iface->addr);
	else if (strcmp(iface->family, "ipv6") == 0)
		ipv6_to_addr(iface->address, iface->addr);
	else if (strcmp(iface->family, "fc") == 0)
		fc_to_addr(iface->address, iface->addr);
	else {
		print_err("unsupported or unspecified address family");
		return;
	}

	if (!strlen(iface->port))
		sprintf(iface->port, "%d", NVME_RDMA_IP_PORT);
}

static int read_dem_config_files(struct host_iface *iface)
{
	struct dirent		*entry;
	DIR			*dir;
	FILE			*fid;
	char			 config_file[FILENAME_MAX + 1];
	int			 count = 0;
	int			 ret;

	dir = opendir(PATH_NVMF_DEM_DISC);
	if (!dir)
		return -errno;

	for_each_dir(entry, dir) {
		if ((strcmp(entry->d_name, CONFIG_FILENAME) == 0) ||
		    (strcmp(entry->d_name, SIGNATURE_FILE_FILENAME) == 0))
			continue;

		snprintf(config_file, FILENAME_MAX, "%s%s",
			 PATH_NVMF_DEM_DISC, entry->d_name);

		fid = fopen(config_file, "r");
		if (fid != NULL) {
			while (!feof(fid))
				read_dem_config(fid, &iface[count]);

			fclose(fid);

			if ((!strcmp(iface[count].type, "")) ||
			    (!strcmp(iface[count].family, "")) ||
			    (!strcmp(iface[count].address, "")))
				print_err("%s: %s.",
					"bad config file. Ignoring interface",
					config_file);
			else {
				translate_addr_to_array(&iface[count]);
				count++;
			}
		} else {
			print_err("Failed to open config file %s", config_file);
			ret = -ENOENT;
			goto out;
		}
	}

	if (count == 0) {
		print_err("No viable interfaces. Exiting");
		ret = -ENODATA;
	}

	ret = 0;
out:
	closedir(dir);
	return ret;
}

int init_interfaces(void)
{
	struct host_iface	*table;
	int			 count;
	int			 ret;

	/* Could avoid this if we move to a list */
	count = count_dem_config_files();
	if (count < 0)
		return count;

	table = calloc(count, sizeof(struct host_iface));
	if (!table)
		return -ENOMEM;

	memset(table, 0, count * sizeof(struct host_iface));

	ret = read_dem_config_files(table);
	if (ret) {
		free(table);
		return -1;
	}

	interfaces = table;

	return count;
}

/*
 * NVMe over Fabrics Distributed Endpoint Manager (NVMe-oF DEM).
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>

#include "json.h"
#include "tags.h"
#include "common.h"

int refresh_target(char *alias)
{
	struct target		*target;

	list_for_each_entry(target, target_list, node)
		if (!strcmp(target->alias, alias)) {
			fetch_log_pages(target);
			return 0;
		}

	return -EINVAL;
}

static void check_host(struct subsystem *subsys, json_t *nqn, json_t *array,
		       const char *host_nqn)
{
	json_t			*obj;
	json_t			*iter;
	struct host		*host;
	int			 i, n;

	n = json_array_size(array);
	for (i = 0; i < n; i++) {
		iter = json_array_get(array, i);
		obj = json_object_get(iter, TAG_HOSTNQN);
		if (obj && json_equal(nqn, obj)) {
			host = malloc(sizeof(*host));
			if (!host)
				return;

			memset(host, 0, sizeof(*host));
			host->subsystem = subsys;
			strcpy(host->nqn, host_nqn);
			list_add(&host->node, &subsys->host_list);
		}
	}
}

static void check_hosts(struct subsystem *subsys, json_t *array, json_t *nqn)
{
	json_t			*grp;
	json_t			*iter;
	json_t			*host;
	int			 i, n;

	n = json_array_size(array);
	for (i = 0; i < n; i++) {
		iter = json_array_get(array, i);
		host = json_object_get(iter, TAG_HOSTNQN);
		if (unlikely(!host))
			continue;
		grp = json_object_get(iter, TAG_ACL);
		if (grp)
			check_host(subsys, nqn, grp, json_string_value(host));
	}
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

		subsys = malloc(sizeof(*subsys));
		if (!subsys)
			return;

		memset(subsys, 0, sizeof(*subsys));
		subsys->target = target;
		strcpy(subsys->nqn, json_string_value(nqn));

		INIT_LIST_HEAD(&subsys->host_list);
		list_add(&subsys->node, &target->subsys_list);

		obj = json_object_get(iter, TAG_ALLOW_ALL);
		if (obj && json_is_integer(obj))
			subsys->access = json_integer_value(obj);

		if (hosts)
			check_hosts(subsys, hosts, nqn);
	}
}

static int get_transport_info(char *alias, json_t *group, char *type,
			      char *fam, char *address, char *port, int *_addr)
{
	json_t			*obj;
	char			*str;
	int			 addr[ADDR_LEN];
	int			 ret;

	obj = json_object_get(group, TAG_TYPE);
	if (!obj) {
		print_err("Controller '%s' error: transport type missing",
			  alias);
		goto out;
	}
	str = (char *) json_string_value(obj);
	strncpy(type, str, CONFIG_TYPE_SIZE);

	obj = json_object_get(group, TAG_FAMILY);
	if (!obj) {
		print_err("Controller '%s' error: transport family missing",
			  alias);
		goto out;
	}
	str = (char *) json_string_value(obj);
	strncpy(fam, str, CONFIG_FAMILY_SIZE);

	obj = json_object_get(group, TAG_ADDRESS);
	if (!obj) {
		print_err("Controller '%s' error: transport address missing",
			  alias);
		goto out;
	}
	str = (char *) json_string_value(obj);

	if (strcmp(fam, "ipv4") == 0)
		ret = ipv4_to_addr(str, addr);
	else if (strcmp(fam, "ipv6") == 0)
		ret = ipv6_to_addr(str, addr);
	else if (strcmp(fam, "fc") == 0)
		ret = fc_to_addr(str, addr);
	else {
		print_err("Controller '%s' error: bad transport family '%s'",
			  alias, fam);
		goto out;
	}

	if (ret < 0) {
		print_err("Controller '%s' error: bad '%s' address '%s'",
			  alias, fam, str);
		goto out;
	}

	memcpy(_addr, addr, sizeof(addr[0]) * ADDR_LEN);
	strncpy(address, str, CONFIG_ADDRESS_SIZE);

	obj = json_object_get(group, TAG_TRSVCID);
	if (obj)
		sprintf(port, "%*lld", CONFIG_PORT_SIZE,
			json_integer_value(obj));
	else
		sprintf(port, "%d", NVME_RDMA_IP_PORT);

	return 1;
out:
	return 0;
}

static int add_to_target_list(json_t *grp, json_t *parent, json_t *hosts)
{
	struct target		*target;
	json_t			*subgroup;
	json_t			*obj;
	int			 addr[ADDR_LEN];
	char			 alias[MAX_ALIAS_SIZE + 1];
	char			 address[CONFIG_ADDRESS_SIZE + 1];
	char			 port[CONFIG_PORT_SIZE + 1];
	char			 fam[CONFIG_FAMILY_SIZE + 1];
	char			 type[CONFIG_TYPE_SIZE + 1];
	int			 refresh = 0;

	obj = json_object_get(parent, TAG_ALIAS);
	if (!obj)
		goto err;

	strncpy(alias, (char *) json_string_value(obj), MAX_ALIAS_SIZE);

	if (!get_transport_info(alias, grp, type, fam, address, port, addr))
		goto err;

	target = malloc(sizeof(*target));
	if (!target)
		goto err;

	memset(target, 0, sizeof(*target));

	INIT_LIST_HEAD(&target->subsys_list);

	list_add(&target->node, target_list);

	obj = json_object_get(parent, TAG_REFRESH);
	if (obj)
		refresh = json_integer_value(obj);

	strncpy(target->alias, alias, MAX_ALIAS_SIZE);

	strncpy(target->trtype, type, CONFIG_TYPE_SIZE);
	strncpy(target->addrfam, fam, CONFIG_FAMILY_SIZE);
	strncpy(target->address, address, CONFIG_ADDRESS_SIZE);
	strncpy(target->port, port, CONFIG_PORT_SIZE);

	memcpy(target->addr, addr, ADDR_LEN);

	target->port_num = atoi(port);
	target->refresh = refresh;

	subgroup = json_object_get(parent, TAG_SUBSYSTEMS);
	if (subgroup)
		check_subsystems(target, subgroup, hosts);

	return 1;
err:
	return 0;
}

int build_target_list(void *context)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*group;
	json_t			*targets;
	json_t			*hosts;
	json_t			*subgroup;
	json_t			*iter;
	int			 i, j, n, cnt;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups)
		return 0;

	cnt = json_array_size(groups);
	for (j = 0; j < cnt; j++) {
		group = json_array_get(groups, i);

		targets = json_object_get(group, TAG_TARGETS);
		if (!targets)
			continue;

		hosts = json_object_get(group, TAG_HOSTS);

		n = json_array_size(targets);
		for (i = 0; i < n; i++) {
			iter = json_array_get(targets, i);
			subgroup = json_object_get(iter, TAG_TRANSPORT);
			if (subgroup)
				add_to_target_list(subgroup, iter, hosts);
		}
	}

	return 0;
}

static int count_dem_config_files(void)
{
	struct dirent		*entry;
	DIR			*dir;
	int			 filecount = 0;

	dir = opendir(PATH_NVMF_DEM_DISC);
	if (dir != NULL) {
		while ((entry = readdir(dir))) {
			if (!strcmp(entry->d_name, "."))
				continue;
			if (!strcmp(entry->d_name, ".."))
				continue;
			filecount++;
		}
	} else {
		print_err("%s does not exist", PATH_NVMF_DEM_DISC);
		filecount = -ENOENT;
	}

	closedir(dir);

	return filecount;
}

static void read_dem_config(FILE *fid, struct interface *iface)
{
	int			 ret;
	char			 tag[LARGEST_TAG + 1];
	char			 val[LARGEST_VAL + 1];

	ret = parse_line(fid, tag, LARGEST_TAG, val, LARGEST_VAL);
	if (ret)
		return;

	if (strcasecmp(tag, TAG_TYPE) == 0)
		strncpy(iface->trtype, val, CONFIG_TYPE_SIZE);
	else if (strcasecmp(tag, TAG_FAMILY) == 0)
		strncpy(iface->addrfam, val, CONFIG_FAMILY_SIZE);
	else if (strcasecmp(tag, TAG_ADDRESS) == 0)
		strncpy(iface->address, val, CONFIG_ADDRESS_SIZE);
	else if (strcasecmp(tag, TAG_TRSVCID) == 0)
		strncpy(iface->pseudo_target_port, val, CONFIG_PORT_SIZE);
}

static void translate_addr_to_array(struct interface *iface)
{
	int			 mask_bits;
	char			*p;

	if (strcmp(iface->addrfam, "ipv4") == 0)
		mask_bits = ipv4_to_addr(iface->address, iface->addr);
	else if (strcmp(iface->addrfam, "ipv6") == 0)
		mask_bits = ipv6_to_addr(iface->address, iface->addr);
	else if (strcmp(iface->addrfam, "fc") == 0)
		mask_bits = fc_to_addr(iface->address, iface->addr);
	else {
		print_err("unsupported or unspecified address family");
		return;
	}

	if (mask_bits) {
		p = strchr(iface->address, '/');
		p[0] = 0;
	}

	if (!strlen(iface->pseudo_target_port))
		sprintf(iface->pseudo_target_port, "%d", NVME_RDMA_IP_PORT);
}

static int read_dem_config_files(struct interface *iface)
{
	struct dirent		*entry;
	DIR			*dir;
	FILE			*fid;
	char			 config_file[FILENAME_MAX + 1];
	int			 count = 0;
	int			 ret;

	dir = opendir(PATH_NVMF_DEM_DISC);
	while ((entry = readdir(dir))) {
		if (!strcmp(entry->d_name, "."))
			continue;

		if (!strcmp(entry->d_name, ".."))
			continue;

		snprintf(config_file, FILENAME_MAX, "%s%s",
			 PATH_NVMF_DEM_DISC, entry->d_name);

		fid = fopen(config_file, "r");
		if (fid != NULL) {
			while (!feof(fid))
				read_dem_config(fid, &iface[count]);

			fclose(fid);

			if ((!strcmp(iface[count].trtype, "")) ||
			    (!strcmp(iface[count].addrfam, "")) ||
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
		free(target);
	}
}

int init_interfaces(void)
{
	struct interface	*table;
	int			 count;
	int			 ret;

	/* Could avoid this if we move to a list */
	count = count_dem_config_files();
	if (count < 0)
		return count;

	table = calloc(count, sizeof(struct interface));
	if (!table)
		return -ENOMEM;

	memset(table, 0, count * sizeof(struct interface));

	ret = read_dem_config_files(table);
	if (ret) {
		free(table);
		return -1;
	}

	interfaces = table;

	return count;
}

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <arpa/inet.h>

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

int usage_target(char *alias, char *results)
{
	struct target		*target;

	list_for_each_entry(target, target_list, node)
		if (!strcmp(target->alias, alias)) {
			sprintf(results, "TODO return Target Usage info");
			return 0;
		}

	return -EINVAL;
}

/* TODO Possible performance improvement, best method to identify valid ACLs
 *	check Hosts againsti the ACL or check the ACL against the Host list
 */
static void check_host(struct subsystem *subsys, json_t *acl, const char *nqn)
{
	json_t			*obj;
	struct host		*host;
	int			 i, n;

	n = json_array_size(acl);
	for (i = 0; i < n; i++) {
		obj = json_array_get(acl, i);
		if (obj && strcmp(nqn, json_string_value(obj)) == 0) {
			host = malloc(sizeof(*host));
			if (!host)
				return;

			memset(host, 0, sizeof(*host));
			host->subsystem = subsys;
			strcpy(host->nqn, nqn);
			list_add_tail(&host->node, &subsys->host_list);
		}
	}
}

static void check_hosts(struct subsystem *subsys, json_t *acl, json_t *hosts)
{
	json_t			*iter;
	json_t			*obj;
	int			 i, n;

	n = json_array_size(hosts);
	for (i = 0; i < n; i++) {
		iter = json_array_get(hosts, i);
		obj = json_object_get(iter, TAG_HOSTNQN);
		if (unlikely(!obj))
			continue;

		check_host(subsys, acl, json_string_value(obj));
	}
}

static void check_subsystems(struct target *target, json_t *array,
			     json_t *hosts)
{
	json_t			*obj;
	json_t			*iter;
	json_t			*acl;
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
		list_add_tail(&subsys->node, &target->subsys_list);

		obj = json_object_get(iter, TAG_ALLOW_ANY);
		if (obj && json_is_integer(obj))
			subsys->access = json_integer_value(obj);

		acl = json_object_get(iter, TAG_HOSTS);

		if (!subsys->access && acl && hosts)
			check_hosts(subsys, acl, hosts);
	}
}

static int get_transport_info(char *alias, json_t *grp, struct port_id *portid)
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

	sprintf(portid->port, "%d", portid->port_num);

	return 1;
out:
	return 0;
}

static struct target *add_to_target_list(json_t *parent, json_t *hosts)
{
	struct target		*target;
	json_t			*subgroup;
	json_t			*obj;
	char			 alias[MAX_ALIAS_SIZE + 1];
	int			 refresh = 0;

	obj = json_object_get(parent, TAG_ALIAS);
	if (!obj)
		goto err;

	strncpy(alias, (char *) json_string_value(obj), MAX_ALIAS_SIZE);

	target = malloc(sizeof(*target));
	if (!target)
		goto err;

	memset(target, 0, sizeof(*target));

	INIT_LIST_HEAD(&target->subsys_list);
	INIT_LIST_HEAD(&target->portid_list);
	INIT_LIST_HEAD(&target->device_list);

	list_add_tail(&target->node, target_list);

	obj = json_object_get(parent, TAG_REFRESH);
	if (obj)
		refresh = json_integer_value(obj);

	strncpy(target->alias, alias, MAX_ALIAS_SIZE);

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
	struct port_id		*portid;

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
		inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
			  s, len);
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
			  s, len);
		break;
	default:
		strncpy(s, "Unknown AF", len);
	}
}

void build_target_list(void)
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

static int count_dem_config_files(void)
{
	struct dirent		*entry;
	DIR			*dir;
	int			 filecount = 0;

	dir = opendir(PATH_NVMF_DEM_DISC);
	if (dir != NULL) {
		for_each_dir(entry, dir)
			filecount++;
		closedir(dir);
	} else {
		print_err("%s does not exist", PATH_NVMF_DEM_DISC);
		filecount = -ENOENT;
	}

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
		strncpy(iface->type, val, CONFIG_TYPE_SIZE);
	else if (strcasecmp(tag, TAG_FAMILY) == 0)
		strncpy(iface->family, val, CONFIG_FAMILY_SIZE);
	else if (strcasecmp(tag, TAG_ADDRESS) == 0)
		strncpy(iface->address, val, CONFIG_ADDRESS_SIZE);
	else if (strcasecmp(tag, TAG_TRSVCID) == 0)
		strncpy(iface->pseudo_target_port, val, CONFIG_PORT_SIZE);
}

static void translate_addr_to_array(struct interface *iface)
{
	int			 mask_bits;
	char			*p;

	if (strcmp(iface->family, "ipv4") == 0)
		mask_bits = ipv4_to_addr(iface->address, iface->addr);
	else if (strcmp(iface->family, "ipv6") == 0)
		mask_bits = ipv6_to_addr(iface->address, iface->addr);
	else if (strcmp(iface->family, "fc") == 0)
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
	for_each_dir(entry, dir) {
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

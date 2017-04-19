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

int refresh_ctrl(char *alias)
{
	struct controller	*ctrl;

	klist_for_each_entry(ctrl,ctrl_list, node)
		if (!strcmp(ctrl->alias, alias)) {
			fetch_log_pages(ctrl);
			return 0;
		}

	return -EINVAL;
}

static void check_host(struct subsystem *subsys, struct json_object *nqn,
		       struct json_object *array)
{
	struct json_object *obj;
	struct json_object *access;
	struct json_object *iter;
	struct host *host;
	int i, n;

	n = json_object_array_length(array);
	for (i = 0; i < n; i++) {
		iter = json_object_array_get_idx(array, i);
		json_object_object_get_ex(iter, TAG_NQN, &obj);
		if (obj && json_object_equal(nqn, obj)) {
			json_object_object_get_ex(iter, TAG_ACCESS, &access);
			if (access && json_object_get_int(access)) {
				host = malloc(sizeof(*host));
				if (!host)
					return;

				memset(host, 0, sizeof(*host));
				host->subsystem = subsys;
				strcpy(host->nqn, json_object_get_string(obj));
				host->access = json_object_get_int(access);
				klist_add(&host->node, &subsys->host_list);
			}
		}
	}
}

static void check_hosts(struct subsystem *subsys, struct json_object *array,
			struct json_object *nqn)
{
	struct json_object *grp;
	struct json_object *iter;
	int i, n;

	n = json_object_array_length(array);
	for (i = 0; i < n; i++) {
		iter = json_object_array_get_idx(array, i);
		json_object_object_get_ex(iter, TAG_ACL, &grp);
		if (grp)
			check_host(subsys, nqn, grp);
	}
}

static void check_subsystems(struct controller *ctrl,
			     struct json_context *ctx,
			     struct json_object *array)
{
	struct json_object *obj;
	struct json_object *iter;
	struct json_object *nqn;
	struct subsystem *subsys;
	int i, n;

	n = json_object_array_length(array);
	for (i = 0; i < n; i++) {
		iter = json_object_array_get_idx(array, i);
		json_object_object_get_ex(iter, TAG_NQN, &nqn);
		if (!nqn)
			continue;

		subsys = malloc(sizeof(*subsys));
		if (!subsys)
			return;

		memset(subsys, 0, sizeof(*subsys));
		subsys->ctrl = ctrl;
		strcpy(subsys->nqn, json_object_get_string(nqn));

		INIT_KLIST_HEAD(&subsys->host_list);
		klist_add(&subsys->node, &ctrl->subsys_list);

		json_object_object_get_ex(iter, TAG_ALLOW_ALL, &obj);
		if (obj && json_object_get_int(obj))
			subsys->access = json_object_get_int(obj);

		if (ctx->hosts)
			check_hosts(subsys, ctx->hosts, nqn);
	}
}

static int match_transport(struct interface *iface, struct json_object *ctrl,
			   char *type, char *fam, char *address, char *port,
			   int *_addr)
{
	struct json_object *obj;
	char *str;
	int family;
	int addr[IPV6_ADDR_LEN];
	int ret;

	family = (strcmp(iface->addrfam, "ipv4") == 0) ? AF_IPV4 :
		(strcmp(iface->addrfam, "ipv6") == 0) ? AF_IPV6 : -1;
	if (family == -1) {
		print_err("Address family not supported\n");
		goto out;
	}

	json_object_object_get_ex(ctrl, TAG_TYPE, &obj);
	if (!obj)
		goto out;
	str = (char *) json_object_get_string(obj);
	if (strcmp(str, iface->trtype))
		goto out;
	strncpy(type, str, CONFIG_TYPE_SIZE);

	json_object_object_get_ex(ctrl, TAG_FAMILY, &obj);
	if (!obj)
		goto out;
	str = (char *) json_object_get_string(obj);
	if (strcmp(str, iface->addrfam))
		goto out;
	strncpy(fam, str, CONFIG_FAMILY_SIZE);

	json_object_object_get_ex(ctrl, TAG_ADDRESS, &obj);
	if (!obj)
		goto out;
	str = (char *) json_object_get_string(obj);
	if (family == AF_IPV4) {
		ret = ipv4_to_addr(str, addr);
		if (ret)
			goto out;
		if (!ipv4_equal(addr, iface->addr, iface->mask))
			goto out;
		memcpy(_addr, addr, sizeof(addr[0]) * IPV4_ADDR_LEN);
	} else {
		ret = ipv6_to_addr(str, addr);
		if (ret)
			goto out;
		if (!ipv6_equal(addr, iface->addr, iface->mask))
			goto out;
		memcpy(_addr, addr, sizeof(addr[0]) * IPV6_ADDR_LEN);
	}
	strncpy(address, str, CONFIG_ADDRESS_SIZE);

	json_object_object_get_ex(ctrl, TAG_PORT, &obj);
	if (obj)
		strncpy(port, (char *) json_object_get_string(obj),
			CONFIG_PORT_SIZE);
	else
		sprintf(port, "%d", NVME_RDMA_IP_PORT);

	return 1;
out:
	return 0;
}

static int check_transport(struct interface *iface, struct json_context *ctx,
			   struct json_object *grp, struct json_object *parent)
{
	struct controller *ctrl;
	struct json_object *subgroup;
	struct json_object *obj;
	int addr[IPV6_ADDR_LEN];
	char address[CONFIG_ADDRESS_SIZE + 1];
	char port[CONFIG_PORT_SIZE + 1];
	char fam[CONFIG_FAMILY_SIZE + 1];
	char type[CONFIG_TYPE_SIZE + 1];
	int refresh = 0;

	if (!match_transport(iface, grp, type, fam, address, port, addr))
		goto err1;

	ctrl = malloc(sizeof(*ctrl));
	if (!ctrl)
		goto err1;

	memset(ctrl, 0, sizeof(*ctrl));

	ctrl->iface = iface;

	INIT_KLIST_HEAD(&ctrl->subsys_list);

	klist_add(&ctrl->node, ctrl_list);

	json_object_object_get_ex(parent, TAG_REFRESH, &obj);
	if (obj)
		refresh = json_object_get_int(obj);

	json_object_object_get_ex(parent, TAG_ALIAS, &obj);
	if (!obj)
		goto err2;

	strncpy(ctrl->alias, (char *) json_object_get_string(obj),
		MAX_ALIAS_SIZE);

	strncpy(ctrl->trtype, type, CONFIG_TYPE_SIZE);
	strncpy(ctrl->addrfam, fam, CONFIG_FAMILY_SIZE);
	strncpy(ctrl->address, address, CONFIG_ADDRESS_SIZE);
	strncpy(ctrl->port, port, CONFIG_PORT_SIZE);

	memcpy(ctrl->addr, addr, IPV6_ADDR_LEN);

	ctrl->port_num = atoi(port);
	ctrl->refresh = refresh;

	json_object_object_get_ex(parent, TAG_SUBSYSTEMS, &subgroup);
	if (subgroup)
		check_subsystems(ctrl, ctx, subgroup);

	return 1;
err2:
	free(ctrl);
err1:
	return 0;
}

int get_transport(struct interface *iface, void *context)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *subgroup;
	struct json_object *iter;
	int i, n;

	if (!iface)
		return -1;
	if (!array)
		return -1;

	n = json_object_array_length(array);
	for (i = 0; i < n; i++) {
		iter = json_object_array_get_idx(array, i);
		json_object_object_get_ex(iter, TAG_TRANSPORT, &subgroup);
		if (subgroup)
			check_transport(iface, ctx, subgroup, iter);
	}

	return 0;
}

int count_dem_config_files()
{
	struct dirent	*entry;
	DIR		*dir;
	int		 filecount = 0;

	dir = opendir(PATH_NVMF_DEM_DISC);
	if (dir != NULL) {
		while ((entry = readdir(dir))) {
			if (!strcmp(entry->d_name,"."))
				continue;
			if (!strcmp(entry->d_name,".."))
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
	int ret;
	char tag[LARGEST_TAG];
	char val[LARGEST_VAL];

	ret = parse_line(fid, tag, sizeof(tag) -1, val, sizeof(val) -1);
	if (ret)
		return;

	if (strcasecmp(tag, TAG_TYPE) == 0)
		strncpy(iface->trtype, val, CONFIG_TYPE_SIZE);
	else if (strcasecmp(tag, TAG_FAMILY) == 0)
		strncpy(iface->addrfam, val, CONFIG_FAMILY_SIZE);
	else if (strcasecmp(tag, TAG_ADDRESS) == 0)
		strncpy(iface->address, val, CONFIG_ADDRESS_SIZE);
	else if (strcasecmp(tag, TAG_NETMASK) == 0)
		strncpy(iface->netmask, val, CONFIG_ADDRESS_SIZE);
	else if (strcasecmp(tag, TAG_PORT) == 0)
		strncpy(iface->pseudo_target_port, val, CONFIG_PORT_SIZE);
}

/* TODO: Support FC and other transports */
static void translate_addr_to_array(struct interface *iface)
{
	char default_port[CONFIG_PORT_SIZE] = {0};

	if (strcmp(iface->addrfam, "ipv4") == 0) {
		ipv4_to_addr(iface->address, iface->addr);
		if (iface->netmask[0] == 0)
			ipv4_mask(iface->mask, 24);
		else
			ipv4_to_addr(iface->netmask, iface->mask);

		sprintf(default_port, "%d", NVME_RDMA_IP_PORT);
	} else if (strcmp(iface->addrfam, "ipv6") == 0) {
		ipv6_to_addr(iface->address, iface->addr);
		if (iface->netmask[0] == 0)
			ipv6_mask(iface->mask, 48);
		else
			ipv6_to_addr(iface->netmask, iface->mask);

		sprintf(default_port, "%d", NVME_RDMA_IP_PORT);
	} else {
		print_err("unsupported or unspecified address family");
		return;
	}

	if (!strlen(iface->pseudo_target_port))
		strcpy(iface->pseudo_target_port, default_port);
}

int read_dem_config_files(struct interface *iface)
{
	struct dirent	*entry;
	DIR		*dir;
	FILE		*fid;
	char		 config_file[FILENAME_MAX+1];
	int		 count = 0;
	int		 ret;

	dir = opendir(PATH_NVMF_DEM_DISC);
	while ((entry = readdir(dir))) {
		if (!strcmp(entry->d_name,"."))
			continue;

		if (!strcmp(entry->d_name,".."))
			continue;

		snprintf(config_file, FILENAME_MAX, "%s%s",
			 PATH_NVMF_DEM_DISC, entry->d_name);

		if ((fid = fopen(config_file,"r")) != NULL) {
			while (!feof(fid))
				read_dem_config(fid, &iface[count]);

			fclose(fid);

			if ((!strcmp(iface[count].trtype, "")) ||
			    (!strcmp(iface[count].addrfam, "")) ||
			    (!strcmp(iface[count].address, "")))
				print_err("%s: bad config file. "
					  "Ignoring interface.", config_file);
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

void cleanup_controllers()
{
	struct controller	*ctrl;
	struct controller	*next_ctrl;
	struct subsystem	*subsys;
	struct subsystem	*next_subsys;
	struct host		*host;
	struct host		*next_host;


	klist_for_each_entry_safe(ctrl, next_ctrl, ctrl_list, node) {
		klist_for_each_entry_safe(subsys, next_subsys,
					  &ctrl->subsys_list, node) {
			klist_for_each_entry_safe(host, next_host,
						  &subsys->host_list, node)
				free(host);

			free(subsys);
		}
		free(ctrl);
	}
}

int init_interfaces()
{
	struct interface	*table;
	int			count;
	int			ret;

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

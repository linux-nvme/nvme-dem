/*
 * Distributed Endpoint Manager.
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
				host->next = subsys->host_list;
				strcpy(host->nqn, json_object_get_string(obj));
				host->access = json_object_get_int(access);
				subsys->host_list = host;
				subsys->num_hosts++;
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
		subsys->next = ctrl->subsystem_list;
		strcpy(subsys->nqn, json_object_get_string(nqn));
		ctrl->subsystem_list = subsys;
		ctrl->num_subsystems++;

		json_object_object_get_ex(iter, TAG_ALLOW_ALL, &obj);
		if (obj && json_object_get_int(obj))
			subsys->access = json_object_get_int(obj);

		if (ctx->hosts)
			check_hosts(subsys, ctx->hosts, nqn);
	}
}

static int match_transport(struct interface *iface, struct json_object *ctrl,
			   int *_addr, char *saddr)
{
	struct json_object *obj;
	char *str;
	int family;
	int addr[IPV6_ADDR_LEN];
	int ret;

	family = (strcmp(iface->addrfam, "ipv4") == 0) ? AF_IPV4 :
		(strcmp(iface->addrfam, "ipv6") == 0) ? AF_IPV6 : -1;
	if (family == -1)
		goto out;

	json_object_object_get_ex(ctrl, TAG_TYPE, &obj);
	if (!obj)
		goto out;
	if (strcmp((char *) json_object_get_string(obj), iface->trtype))
		goto out;
	json_object_object_get_ex(ctrl, TAG_FAMILY, &obj);
	if (!obj)
		goto out;
	str = (char *) json_object_get_string(obj);
	if (strcmp(str, iface->addrfam))
		goto out;
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

	strncpy(saddr, str, CONFIG_ADDRESS_SIZE);

	return 1;
out:
	return 0;
}

static int check_transport(struct interface *iface, struct json_context *ctx,
			   struct json_object *grp, struct json_object *parent)
{
	struct controller *ctrl;
	struct json_object *subgroup;
	int addr[IPV6_ADDR_LEN];
	char str[CONFIG_ADDRESS_SIZE];

	if (!match_transport(iface, grp, addr, str))
		goto err;

	ctrl = malloc(sizeof(*ctrl));
	if (!ctrl)
		goto err;

	memset(ctrl, 0, sizeof(*ctrl));
	ctrl->next = iface->controller_list;
	ctrl->interface = iface;
	iface->controller_list = ctrl;
	iface->num_controllers++;

	strncpy(ctrl->address, str, CONFIG_ADDRESS_SIZE);
	memcpy(ctrl->addr, addr, IPV6_ADDR_LEN);

	json_object_object_get_ex(parent, TAG_SUBSYSTEMS, &subgroup);
	if (subgroup)
		check_subsystems(ctrl, ctx, subgroup);

	return 1;
err:
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
		print_err("%s does not exist\n", PATH_NVMF_DEM_DISC);
		filecount = -ENOENT;
	}

	closedir(dir);

	print_debug("Found %d files", filecount);

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

	if (strcmp(tag, "Type") == 0) {
		strncpy(iface->trtype, val, CONFIG_TYPE_SIZE);
		print_debug("%s %s", tag, iface->trtype);
	} else if (strcmp(tag, "Family") == 0) {
		strncpy(iface->addrfam, val, CONFIG_FAMILY_SIZE);
		print_debug("%s %s",tag, iface->addrfam);
	} else if (strcmp(tag, "Address") == 0) {
		strncpy(iface->hostaddr, val, CONFIG_ADDRESS_SIZE);
		print_debug("%s %s",tag, iface->hostaddr);
	} else if (strcmp(tag, "Netmask") == 0) {
		strncpy(iface->netmask, val, CONFIG_ADDRESS_SIZE);
		print_debug("%s %s", tag, iface->netmask);
	}
}

static void translate_addr_to_array(struct interface *iface)
{
	if (strcmp(iface->addrfam, "ipv4") == 0) {
		ipv4_to_addr(iface->hostaddr, iface->addr);
		if (iface->netmask[0] == 0)
			ipv4_mask(iface->mask, 24);
		else
			ipv4_to_addr(iface->netmask, iface->mask);
	} else {
		ipv6_to_addr(iface->hostaddr, iface->addr);
		if (iface->netmask[0] == 0)
			ipv6_mask(iface->mask, 48);
		else
			ipv6_to_addr(iface->netmask, iface->mask);
	}
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

		print_debug("path = %s", config_file);
		if ((fid = fopen(config_file,"r")) != NULL){
			print_debug("Opening %s",config_file);

			iface[count].interface_id = count;
			while (!feof(fid))
				read_dem_config(fid, &iface[count]);
			fclose(fid);

			if ((!strcmp(iface[count].trtype, "")) ||
			    (!strcmp(iface[count].addrfam, "")) ||
			    (!strcmp(iface[count].hostaddr, "")))
				print_err("%s: bad config file. "
					"Ignoring interface.", config_file);
			else {
				translate_addr_to_array(&iface[count]);
				count++;
			}
		} else {
			print_err("Failed to open config file %s\n",
				config_file);
			ret = -ENOENT;
			goto out;
		}
	}

	if (count == 0) {
		print_err("No viable interfaces. Exiting\n");
		ret = -ENODATA;
	}

	/* TODO: Validate no two ifaces share the same subnet/mask */

	ret = 0;
out:
	closedir(dir);
	return ret;
}

int init_interfaces(struct interface **interfaces)
{
	struct interface *iface;
	int		  count;
	int		  ret;

	/* Could avoid this if we move to a list */
	count = count_dem_config_files();
	if (count < 0)
		return count;

	iface = calloc(count, sizeof (struct interface));
	ret = read_dem_config_files(iface);
	if (ret)
		return -1;

	*interfaces = iface;

	return count;
}

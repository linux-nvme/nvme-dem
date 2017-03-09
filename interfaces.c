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
#include <dirent.h>

#include "json.h"
#include "tags.h"
#include "common.h"

static int check_transport(struct interface *iface, struct json_object *grp)
{
	struct json_object *obj;
	struct controller *ctrl;
	int addr[IPV6_ADDR_LEN];
	char *str;
	int family;
	int ret = 0;

	family = (strcmp(iface->addrfam, "ipv4") == 0) ? AF_IPV4 :
		 (strcmp(iface->addrfam, "ipv6") == 0) ? AF_IPV6 : -1;
	if (family == -1)
		goto out;

	json_object_object_get_ex(grp, TAG_TYPE, &obj);
	if (!obj)
		goto out;
	if (strcmp((char *) json_object_get_string(obj), iface->trtype))
		goto out;
	json_object_object_get_ex(grp, TAG_FAMILY, &obj);
	if (!obj)
		goto out;
	str = (char *) json_object_get_string(obj);
	if (strcmp(str, iface->addrfam))
		goto out;
	json_object_object_get_ex(grp, TAG_ADDRESS, &obj);
	if (!obj)
		goto out;
	str = (char *) json_object_get_string(obj);
	if (family == AF_IPV4) {
		ret = ipv4_to_addr(str, addr);
		if (ret)
			goto out;
		if (!ipv4_equal(addr, iface->addr, iface->mask))
			goto out;
	} else {
		ret = ipv6_to_addr(str, addr);
		if (ret)
			goto out;
		if (!ipv6_equal(addr, iface->addr, iface->mask))
			goto out;
	}

	ctrl = malloc(sizeof(*ctrl));
	if (!ctrl)
		goto out;

	ctrl->next = iface->controller_list;
	ctrl->interface = iface;
	iface->controller_list = ctrl;
	iface->num_controllers++;

	strncpy(ctrl->address, str, CONFIG_ADDRESS_SIZE);
	memcpy(ctrl->addr, addr, IPV6_ADDR_LEN);

	ret = 1;
out:
	return ret;
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
		if (!subgroup)
			continue;

		if (!check_transport(iface, subgroup))
			continue;
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
		fprintf(stderr, "%s does not exist\n", PATH_NVMF_DEM_DISC);
		filecount = -ENOENT;
	}

	closedir(dir);

	print_debug("Found %d files", filecount);

	return filecount;
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
			char tag[LARGEST_TAG];
			char val[LARGEST_VAL];

			print_debug("Opening %s",config_file);

			iface[count].interface_id = count;
			while (!feof(fid)) {
				ret = parse_line(fid, tag, sizeof(tag) -1, val, sizeof(val) -1);
				if (ret)
					continue;
				if (!strcmp(tag, "Type")) {
					strncpy(iface[count].trtype, val, CONFIG_TYPE_SIZE);
					print_debug("%s %s", tag, iface[count].trtype);
					continue;
				}
				if (!strcmp(tag, "Family")) {
					strncpy(iface[count].addrfam, val, CONFIG_FAMILY_SIZE);
					print_debug("%s %s",tag, iface[count].addrfam);
					continue;
				}
				if (!strcmp(tag, "Address")) {
					strncpy(iface[count].hostaddr, val, CONFIG_ADDRESS_SIZE);
					print_debug("%s %s",tag, iface[count].hostaddr);
					continue;
				}
				if (!strcmp(tag, "Netmask")) {
					strncpy(iface[count].netmask, val, CONFIG_ADDRESS_SIZE);
					print_debug("%s %s", tag, iface[count].netmask);
				}
			}
			fclose(fid);

			if ((!strcmp(iface[count].trtype, "")) || (!strcmp(iface[count].addrfam, "")) ||
			    (!strcmp(iface[count].hostaddr, "")))
				fprintf(stderr, "%s: bad config file. Ignoring interface.\n", config_file);
			else {
				if (strcmp(iface[count].addrfam, "ipv4") == 0) {
					ipv4_to_addr(iface[count].hostaddr, iface[count].addr);
					if (iface[count].netmask[0] == 0)
						ipv4_mask(iface[count].mask, 24);
					else
						ipv4_to_addr(iface[count].netmask, iface[count].mask);
				} else {
					ipv6_to_addr(iface[count].hostaddr, iface[count].addr);
					if (iface[count].netmask[0] == 0)
						ipv6_mask(iface[count].mask, 48);
					else
						ipv6_to_addr(iface[count].netmask, iface[count].mask);
				}
				count++;
			}
		} else {
			fprintf(stderr, "Failed to open config file %s\n", config_file);
			ret = -ENOENT;
			goto out;
		}
	}

	if (count == 0) {
		fprintf(stderr, "No viable interfaces. Exiting\n");
		ret = -ENODATA;
	}

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


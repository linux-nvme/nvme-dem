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

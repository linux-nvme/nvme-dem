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

#include <jansson.h>
#include "tags.h"

#include "show.h"

static int list_array(json_t *array, int indent)
{
	json_t			*obj;
	int			 i, cnt;

	cnt = json_array_size(array);
	if (!cnt)
		return 0;

	for (i = 0; i < cnt; i++) {
		obj = json_array_get(array, i);
		printf("%s%s%s", i ? "\n" : "", indent ? "  " : "",
		       json_string_value(obj));
	}

	printf("\n");

	return cnt;
}

void show_group_list(json_t *parent)
{
	json_t			*array;
	int			 cnt = 0;

	array = json_object_get(parent, TAG_GROUPS);
	if (array)
		cnt = list_array(array, 0);

	if (!cnt)
		printf("No %s defined\n", TAG_GROUPS);
}

void show_target_list(json_t *parent, int indent)
{
	json_t			*array;
	int			 cnt = 0;

	array = json_object_get(parent, TAG_TARGETS);
	if (array)
		cnt = list_array(array, indent);

	if (!cnt)
		printf("%sNo %s defined\n", indent ? "  " : "", TAG_TARGETS);
}

static void show_nsids(json_t *parent)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	char			 tab[] = "      ";
	int			 i, cnt;
	int			 devid = INVALID_DEVID;

	array = json_object_get(parent, TAG_NSIDS);

	cnt = (array) ? json_array_size(array) : 0;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NSID);
		if (!obj)
			continue;

		printf("%snsid %lld:", i ? ", " : tab, json_integer_value(obj));

		obj = json_object_get(iter, TAG_DEVID);
		if (obj) {
			devid = json_integer_value(obj);
			if (devid != NULL_BLK_DEVID)
				printf(" nvme%d", devid);
			else
				printf(" nullb0");
		}

		obj = json_object_get(iter, TAG_DEVNSID);
		if (obj && devid >= 0)
			printf("n%lld", json_integer_value(obj));
	}

	if (cnt)
		printf("\n");
}

static void show_acl(json_t *parent)
{
	json_t			*array;
	json_t			*iter;
	char			 tab[] = "      ";
	int			 i, cnt;

	array = json_object_get(parent, TAG_HOSTS);

	cnt = (array) ? json_array_size(array) : 0;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		printf("%s%s", i ? ", " : tab, json_string_value(iter));
	}

	if (cnt)
		printf("\n");
}

static void show_subsystems(json_t *parent)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	int			 i, cnt;

	printf("  %s\n", TAG_SUBSYSTEMS);

	array = json_object_get(parent, TAG_SUBSYSTEMS);

	cnt = array ? json_array_size(array) : 0;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_SUBNQN);
		if (!obj)
			continue;

		printf("    %s ", json_string_value(obj));

		obj = json_object_get(iter, TAG_ALLOW_ANY);
		if (obj) {
			if (json_integer_value(obj))
				printf("(allow_any_host)\n");
			else
				printf("(restricted)\n");
		}

		show_acl(iter);

		show_nsids(iter);
	}

	if (!cnt)
		printf("    No Subsystems defined\n");
}

static void show_oob_interface(json_t *iter)
{
	json_t			*obj;

	obj = json_object_get(iter, TAG_IFFAMILY);
	if (obj)
		printf("%s ", json_string_value(obj));

	obj = json_object_get(iter, TAG_IFADDRESS);
	if (obj)
		printf("%s ", json_string_value(obj));

	obj = json_object_get(iter, TAG_IFPORT);
	if (obj)
		printf("%lld", json_integer_value(obj));
}

static void show_inb_interface(json_t *iter)
{
	json_t			*obj;

	obj = json_object_get(iter, TAG_TYPE);
	if (obj)
		printf("%s ", json_string_value(obj));

	obj = json_object_get(iter, TAG_FAMILY);
	if (obj)
		printf("%s ", json_string_value(obj));

	obj = json_object_get(iter, TAG_ADDRESS);
	if (obj)
		printf("%s ", json_string_value(obj));

	obj = json_object_get(iter, TAG_TRSVCID);
	if (obj)
		printf("%lld", json_integer_value(obj));
}

static void show_transport(json_t *iter, int indent)
{
	json_t			*obj;
	char			 tab[8];
	const char		*typ = NULL, *fam;

	memset(tab, 0, sizeof(tab));
	memset(tab, ' ', indent);

	obj = json_object_get(iter, TAG_PORTID);
	if (obj)
		printf("%s%lld: ", tab, json_integer_value(obj));

	obj = json_object_get(iter, TAG_TYPE);
	if (obj) {
		typ = json_string_value(obj);
		printf("%s ", typ);
	}

	obj = json_object_get(iter, TAG_FAMILY);
	if (obj) {
		fam = json_string_value(obj);
		if (typ && strcmp(typ, fam))
			printf("%s ", fam);
	}

	obj = json_object_get(iter, TAG_ADDRESS);
	if (obj)
		printf("%s ", json_string_value(obj));

	obj = json_object_get(iter, TAG_TRSVCID);
	if (obj)
		printf("%lld", json_integer_value(obj));

#if 0 // TODO is treq needed and if so translate it to words
	obj = json_object_get(iter, TAG_TREQ);
	if (obj)
		printf("%lld ", json_integer_value(obj));
#endif
}

static void show_portids(json_t *parent)
{
	json_t			*array;
	json_t			*iter;
	int			 i, cnt = 0;

	array = json_object_get(parent, TAG_PORTIDS);
	if (array)
		cnt = json_array_size(array);

	printf("  %s\n", TAG_PORTIDS);

	for (i = 0; i < cnt; i++) {
		if (i)
			printf("\n");

		iter = json_array_get(array, i);
		show_transport(iter, 4);
	}

	if (cnt)
		printf("\n");
	else
		printf("    No Port IDs defined\n");
}

static void show_devices(json_t *parent)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	int			 devid;
	int			 i, cnt = 0;

	array = json_object_get(parent, TAG_NSDEVS);
	if (array)
		cnt = json_array_size(array);

	if (!cnt)
		return;

	printf("  %s:", TAG_NSDEVS);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_DEVID);
		if (!obj || !json_is_integer(obj)) {
			printf(" syntax error\n");
			return;
		}

		devid = json_integer_value(obj);

		if (devid == NULL_BLK_DEVID) {
			printf(" nullb0");
			continue;
		}

		obj = json_object_get(iter, TAG_DEVNSID);
		if (!obj || !json_is_integer(obj)) {
			printf(" syntax error\n");
			return;
		}

		printf(" nvme%dn%lld", devid, json_integer_value(obj));
	}

	if (cnt)
		printf("\n");
	else
		printf("No devices configured\n");
}

static void show_fabric_ifaces(json_t *parent)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	const char		*typ, *fam;
	int			 i, cnt = 0;

	array = json_object_get(parent, TAG_INTERFACES);
	if (array)
		cnt = json_array_size(array);

	if (!cnt)
		return;

	printf("  %s:", TAG_INTERFACES);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_TYPE);
		if (!obj || !json_is_string(obj))
			goto err;

		typ = json_string_value(obj);

		obj = json_object_get(iter, TAG_FAMILY);
		if (!obj || !json_is_string(obj))
			goto err;

		if (strcmp(typ, json_string_value(obj)))
			fam = json_string_value(obj);
		else
			fam = NULL;

		obj = json_object_get(iter, TAG_ADDRESS);
		if (!obj || !json_is_string(obj))
			goto err;

		if (fam)
			printf("\n    %d: %s %s %s",
			       i, typ, fam, json_string_value(obj));
		else
			printf("\n    %d: %s %s",
			       i, typ, json_string_value(obj));
	}

	if (cnt)
		printf("\n");
	else
		printf("No devices configured\n");
	return;
err:
	printf(" syntax error\n");
}

void show_usage_data(json_t *parent)
{
	// TODO show usage data is human readable format
	UNUSED(parent);
}

void show_target_data(json_t *parent)
{
	json_t			*attrs;
	const char		*mode;
	int			 refresh;

	attrs = json_object_get(parent, TAG_ALIAS);
	if (!attrs)
		return;

	printf("%s '%s' ", TAG_TARGET, json_string_value(attrs));

	attrs = json_object_get(parent, TAG_REFRESH);
	if (attrs) {
		refresh = json_integer_value(attrs);
		if (refresh)
			printf("%s every %d minutes", TAG_REFRESH, refresh);
		else
			printf("%s disabled", TAG_REFRESH);
	}

	attrs = json_object_get(parent, TAG_MGMT_MODE);
	if (attrs) {
		mode = json_string_value(attrs);
		printf("\n  Management Mode: ");
		if (strcmp(mode, TAG_LOCAL_MGMT) == 0)
			printf("Local");
		else if (strcmp(mode, TAG_IN_BAND_MGMT) == 0) {
			printf("In-Band - ");
			attrs = json_object_get(parent, TAG_INTERFACE);
			if (attrs)
				show_inb_interface(attrs);
			else
				printf("Interface undefined");
		} else if (strcmp(mode, TAG_OUT_OF_BAND_MGMT) == 0) {
			printf("Out-of-Band - ");
			attrs = json_object_get(parent, TAG_INTERFACE);
			if (attrs)
				show_oob_interface(attrs);
			else
				printf("Interface undefined");
		} else
			printf("\n%s %s", TAG_MGMT_MODE, mode);
	} else
		printf("\n  Management Mode: undefined (default to Local)");

	printf("\n");

	show_fabric_ifaces(parent);

	show_devices(parent);

	show_portids(parent);

	show_subsystems(parent);

	printf("\n");
}

void show_host_list(json_t *parent, int indent)
{
	json_t			*array;
	int			 cnt = 0;

	array = json_object_get(parent, TAG_HOSTS);
	if (array)
		cnt = list_array(array, indent);

	if (!cnt)
		printf("%sNo %s defined\n", indent ? "  " : "", TAG_HOSTS);
}

static void show_allowed_list(json_t *list, char *tag)
{
	json_t			*obj;
	json_t			*alias;
	json_t			*nqn;
	int			 i, n;

	printf("\n  %s: ", tag);

	n = json_array_size(list);
	for (i = 0; i < n; i++) {
		obj = json_array_get(list, i);
		alias = json_object_get(obj, TAG_ALIAS);
		nqn = json_object_get(obj, TAG_SUBNQN);
		if (alias && json_is_string(alias) &&
		    nqn && json_is_string(nqn)) {
			printf("%s%s / %s", i ? ", " : "",
			       json_string_value(alias),
			       json_string_value(nqn));
		}
	}
}

void show_host_data(json_t *parent)
{
	json_t			*obj;
	json_t			*list;

	obj = json_object_get(parent, TAG_ALIAS);
	if (!obj)
		return;

	printf("%s '%s'", TAG_HOST, json_string_value(obj));

	obj = json_object_get(parent, TAG_HOSTNQN);
	if (obj)
		printf(", %s '%s'", TAG_HOSTNQN, json_string_value(obj));

	list = json_object_get(parent, TAG_SHARED);
	if (list)
		show_allowed_list(list, TAG_SHARED);

	list = json_object_get(parent, TAG_RESTRICTED);
	if (list)
		show_allowed_list(list, TAG_RESTRICTED);

	printf("\n");
}

void show_group_data(json_t *parent)
{
	json_t			*obj;

	obj = json_object_get(parent, TAG_NAME);
	if (!obj) {
		printf("no %s name?\n", TAG_GROUP);
		return;
	}

	printf("%s '%s'\n", TAG_GROUP, json_string_value(obj));

	printf("%s:\n", TAG_TARGETS);

	show_target_list(parent, 1);

	printf("%s:\n", TAG_HOSTS);

	show_host_list(parent, 1);

	printf("\n");
}

void show_config(json_t *parent)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	const char		*typ = NULL, *fam;
	int			 i, cnt;

	array = json_object_get(parent, TAG_INTERFACES);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_ID);
		if (!obj)
			continue;

		printf("%s%lld: ", (i) ? "\n" : "", json_integer_value(obj));

		obj = json_object_get(iter, TAG_TYPE);
		if (obj) {
			typ = json_string_value(obj);
			printf("%s ", typ);
		}

		obj = json_object_get(iter, TAG_FAMILY);
		if (obj) {
			fam = json_string_value(obj);
			if (typ && strcmp(typ, fam))
				printf("%s ", fam);
		}

		obj = json_object_get(iter, TAG_ADDRESS);
		if (obj)
			printf("%s", json_string_value(obj));

		obj = json_object_get(iter, TAG_TRSVCID);
		if (obj)
			printf(":%lld", json_integer_value(obj));
	}

	printf("\n");

	return;
err:
	printf("No Interfaces defined\n");
}

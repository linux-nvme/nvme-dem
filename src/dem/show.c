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

#include <jansson.h>
#include "tags.h"

#include "show.h"

#define FMT_TAG		"\"%s\": "
#define FMT_STR		FMT_TAG "\"%s\""
#define FMT_INT		FMT_TAG "%lld"
#define FMT_NUM		FMT_TAG "%d"
#define FMT_VAL		"\"%s\""

static int list_array(json_t *array, int formatted, int indent)
{
	json_t			*obj;
	int			 i, cnt;

	cnt = json_array_size(array);
	if (!cnt)
		return 0;

	for (i = 0; i < cnt; i++) {
		obj = json_array_get(array, i);
		if (formatted)
			printf("%s" FMT_VAL, i ? ", " : "",
			       json_string_value(obj));
		else
			printf("%s%s%s", i ? "\n" : "", indent ? "  " : "",
			       json_string_value(obj));
	}

	if (!formatted)
		printf("\n");

	return cnt;
}

void show_group_list(json_t *parent, int formatted)
{
	json_t			*array;
	int			 cnt = 0;

	if (formatted)
		printf("{ " FMT_TAG "[ ", TAG_GROUPS);

	array = json_object_get(parent, TAG_GROUPS);
	if (array)
		cnt = list_array(array, formatted, 0);

	if (!cnt && !formatted)
		printf("No %s defined\n", TAG_GROUPS);
	else if (formatted)
		printf("%s] }\n", cnt ? " " : "");
}

void show_target_list(json_t *parent, int formatted, int indent)
{
	json_t			*array;
	int			 cnt = 0;

	if (!indent && formatted)
		printf("{\n");

	if (formatted)
		printf("  " FMT_TAG "[ ", TAG_TARGETS);

	array = json_object_get(parent, TAG_TARGETS);
	if (array)
		cnt = list_array(array, formatted, indent);

	if (!cnt && !formatted)
		printf("%sNo %s defined\n", indent ? "  " : "", TAG_TARGETS);
	else if (formatted)
		printf("%s]%s\n", cnt ? " " : "", indent ? "," : "");

	if (!indent && formatted)
		printf("}\n");
}

static void show_nsids(json_t *parent, int formatted)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	char			 tab[] = "      ";
	int			 i, cnt;

	array = json_object_get(parent, TAG_NSIDS);

	cnt = (array) ? json_array_size(array) : 0;

	if (formatted)
		printf("      " FMT_TAG "[", TAG_NSIDS);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NSID);
		if (!obj)
			continue;

		if (formatted)
			printf("%s\n%s  { " FMT_INT, i ? "," : "",
			       tab, TAG_NSID, json_integer_value(obj));
		else
			printf("%snsid %lld:", i ? ", " : tab,
			       json_integer_value(obj));

		obj = json_object_get(iter, TAG_DEVID);
		if (obj) {
			if (formatted)
				printf(", " FMT_INT " ", TAG_DEVID,
				       json_integer_value(obj));
			else
				printf("nvme%lld", json_integer_value(obj));
		}

		obj = json_object_get(iter, TAG_DEVNSID);
		if (obj) {
			if (formatted)
				printf(", " FMT_INT " ", TAG_DEVNSID,
				       json_integer_value(obj));
			else
				printf("n%lld", json_integer_value(obj));
		}

		if (formatted)
			printf("}");
	}

	if (formatted)
		printf("%s ],\n", cnt ? "\n      " : "");
	else if (cnt)
		printf("\n");
}

static void show_acl(json_t *parent, int formatted)
{
	json_t			*array;
	json_t			*iter;
	char			 tab[] = "      ";
	int			 i, cnt;

	array = json_object_get(parent, TAG_HOSTS);

	cnt = (array) ? json_array_size(array) : 0;

	if (formatted)
		printf("      " FMT_TAG "[", TAG_HOSTS);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		if (formatted)
			printf(" %s\"%s\"", i ? "," : "",
			       json_string_value(iter));
		else
			printf("%s%s", i ? ", " : tab,
			       json_string_value(iter));
	}

	if (formatted)
		printf(" ],\n");
	else if (cnt)
		printf("\n");
}

static void show_subsystems(json_t *parent, int formatted)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	int			 i, cnt;

	if (formatted)
		printf("  " FMT_TAG "[", TAG_SUBSYSTEMS);
	else
		printf("  %s\n", TAG_SUBSYSTEMS);

	array = json_object_get(parent, TAG_SUBSYSTEMS);

	cnt = array ? json_array_size(array) : 0;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_SUBNQN);
		if (!obj)
			continue;

		if (formatted)
			printf("%s {\n      " FMT_STR, i ? "," : "",
			       TAG_SUBNQN, json_string_value(obj));
		else
			printf("    %s ", json_string_value(obj));

		obj = json_object_get(iter, TAG_ALLOW_ANY);
		if (obj) {
			if (formatted)
				printf(", " FMT_INT ",\n",
				       TAG_ALLOW_ANY,
				       json_integer_value(obj));
			else if (json_integer_value(obj))
				printf("(allow_any_host)\n");
			else
				printf("(restricted)\n");
		}

		show_acl(iter, formatted);

		show_nsids(iter, formatted);

		if (formatted)
			printf("    }");
	}

	if (formatted)
		printf("%s ],\n", cnt ? "\n " : "");
	else if (!cnt)
		printf("    No Subsystems defined\n");
}

static void show_interface(json_t *iter, int formatted, int indent)
{
	json_t			*obj;
	char			 tab[8];

	memset(tab, 0, 16);

	if (formatted) {
		memset(tab, ' ', indent - 2);
		printf(",\n%s" FMT_TAG "{", tab, TAG_INTERFACE);
	}

	memset(tab, ' ', indent);

	obj = json_object_get(iter, TAG_IFFAMILY);
	if (obj) {
		if (formatted)
			printf("\n%s" FMT_STR, tab,
			       TAG_IFFAMILY, json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_IFADDRESS);
	if (obj) {
		if (formatted)
			printf(",\n%s" FMT_STR, tab,
			       TAG_IFADDRESS, json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_IFPORT);
	if (obj) {
		if (formatted)
			printf(",\n%s" FMT_INT, tab,
			       TAG_IFPORT, json_integer_value(obj));
		else
			printf("%lld", json_integer_value(obj));
	}

	if (formatted) {
		tab[indent-2] = 0;
		printf("\n%s},", tab);
	}
}

static void show_transport(json_t *iter, int formatted, int indent)
{
	json_t			*obj;
	char			 tab[8];
	const char		*typ = NULL, *fam;

	memset(tab, 0, 16);

	if (formatted) {
		memset(tab, ' ', indent);
		printf("\n%s{", tab);
		indent += 2;
	}

	memset(tab, ' ', indent);

	obj = json_object_get(iter, TAG_PORTID);
	if (obj) {
		if (formatted)
			printf("\n%s" FMT_INT, tab,
			       TAG_PORTID, json_integer_value(obj));
		else
			printf("%s%lld: ", tab, json_integer_value(obj));
	}

	obj = json_object_get(iter, TAG_TYPE);
	if (obj) {
		typ = json_string_value(obj);
		if (formatted)
			printf(",\n%s" FMT_STR, tab, TAG_TYPE, typ);
		else
			printf("%s ", typ);
	}

	obj = json_object_get(iter, TAG_TREQ);
	if (obj) {
		if (formatted)
			printf(",\n%s" FMT_INT, tab,
			       TAG_TREQ, json_integer_value(obj));
		else
			printf("%lld ", json_integer_value(obj));
	}

	obj = json_object_get(iter, TAG_FAMILY);
	if (obj) {
		fam = json_string_value(obj);
		if (formatted)
			printf(",\n%s" FMT_STR, tab, TAG_FAMILY, fam);
		else if (typ && strcmp(typ, fam))
			printf("%s ", fam);
	}

	obj = json_object_get(iter, TAG_ADDRESS);
	if (obj) {
		if (formatted)
			printf(",\n%s" FMT_STR, tab,
			       TAG_ADDRESS, json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_TRSVCID);
	if (obj) {
		if (formatted)
			printf(",\n%s" FMT_INT, tab,
			       TAG_TRSVCID, json_integer_value(obj));
		else
			printf("%lld", json_integer_value(obj));
	}

	if (formatted) {
		tab[indent-2] = 0;
		printf("\n%s},", tab);
	}
}

static void show_portids(json_t *parent, int formatted)
{
	json_t			*array;
	json_t			*iter;
	int			 i, cnt = 0;

	array = json_object_get(parent, TAG_PORTIDS);
	if (array)
		cnt = json_array_size(array);

	if (formatted)
		printf("  " FMT_TAG "[", TAG_PORTIDS);
	else
		printf("  %s\n", TAG_PORTIDS);

	for (i = 0; i < cnt; i++) {
		if (!formatted && i)
			printf("\n");

		iter = json_array_get(array, i);
		show_transport(iter, formatted, 4);
	}

	if (cnt)
		printf("\n");

	if (formatted)
		printf("%s ],\n", cnt ? " " : "");
	else if (!cnt)
		printf("    No Port IDs defined\n");
}

void show_usage_data(json_t *parent, int formatted)
{
	// TODO show usage data is human readable format

	json_dumps(parent, formatted ? 1 : 0);
}

void show_target_data(json_t *parent, int formatted)
{
	json_t			*attrs;
	const char		*mode;
	int			 refresh;

	attrs = json_object_get(parent, TAG_ALIAS);
	if (!attrs)
		return;

	if (formatted)
		printf("{\n  " FMT_TAG FMT_VAL, TAG_ALIAS,
		       json_string_value(attrs));
	else
		printf("%s '%s' ", TAG_TARGET, json_string_value(attrs));

	attrs = json_object_get(parent, TAG_REFRESH);
	if (attrs) {
		refresh = json_integer_value(attrs);
		if (formatted)
			printf(",\n  " FMT_NUM, TAG_REFRESH, refresh);
		else if (refresh)
			printf("%s every %d minutes", TAG_REFRESH, refresh);
		else
			printf("%s disabled", TAG_REFRESH);
	}

	attrs = json_object_get(parent, TAG_MGMT_MODE);
	if (attrs) {
		mode = json_string_value(attrs);
		if (formatted) {
			printf(",\n  " FMT_STR, TAG_MGMT_MODE, mode);
			attrs = json_object_get(parent, TAG_INTERFACE);
			if (attrs)
				show_interface(attrs, formatted, 4);
		} else {
			printf("\n  Management Mode: ");
			if (strcmp(mode, TAG_LOCAL_MGMT) == 0)
				printf("Local");
			else if (strcmp(mode, TAG_IN_BAND_MGMT) == 0)
				printf("In-Band");
			else if (strcmp(mode, TAG_OUT_OF_BAND_MGMT) == 0) {
				printf("Out-of-Band - ");
				attrs = json_object_get(parent, TAG_INTERFACE);
				if (attrs)
					show_interface(attrs, formatted, 4);
				else
					printf("Interface undefined");
			} else
				printf("\n%s %s", TAG_MGMT_MODE,
				       json_string_value(attrs));
		}
	} else if (!formatted)
		printf("\n  Management Mode: undefined (default to Local)");

	printf("\n");

	show_portids(parent, formatted);

	show_subsystems(parent, formatted);

	if (formatted)
		printf("}");

	printf("\n");
}

void show_host_list(json_t *parent, int formatted, int indent)
{
	json_t			*array;
	int			 cnt = 0;

	if (!indent && formatted)
		printf("{\n");

	if (formatted)
		printf("  ");

	if (formatted)
		printf(FMT_TAG "[ ", TAG_HOSTS);

	array = json_object_get(parent, TAG_HOSTS);
	if (array)
		cnt = list_array(array, formatted, indent);

	if (!cnt && !formatted)
		printf("%sNo %s defined\n", indent ? "  " : "", TAG_HOSTS);
	else if (formatted)
		printf("%s]%s\n", cnt ? " " : "", indent ? "," : "");

	if (!indent && formatted)
		printf("}\n");
}

static void show_allowed_list(json_t *list, int formatted, char *tag)
{
	json_t			*obj;
	json_t			*alias;
	json_t			*nqn;
	int			 i, n;

	if (formatted)
		printf(",\n  " FMT_TAG "[", tag);
	else
		printf("\n  %s: ", tag);

	n = json_array_size(list);
	for (i = 0; i < n; i++) {
		obj = json_array_get(list, i);
		alias = json_object_get(obj, TAG_ALIAS);
		nqn = json_object_get(obj, TAG_SUBNQN);
		if (alias && json_is_string(alias) &&
		    nqn && json_is_string(nqn)) {
			if (formatted)
				printf("%s\n    {" FMT_STR ", " FMT_STR "}",
				       i ? "," : "",
				       TAG_ALIAS, json_string_value(alias),
				       TAG_SUBNQN, json_string_value(nqn));
			else
				printf("%s%s / %s", i ? ", " : "",
				       json_string_value(alias),
				       json_string_value(nqn));
		}
	}

	if (formatted)
		printf("%s ]", n ? "\n " : "");
}

void show_host_data(json_t *parent, int formatted)
{
	json_t			*obj;
	json_t			*list;

	obj = json_object_get(parent, TAG_ALIAS);
	if (!obj)
		return;

	if (formatted)
		printf("{\n  " FMT_STR, TAG_ALIAS, json_string_value(obj));
	else
		printf("%s '%s'", TAG_HOST, json_string_value(obj));

	obj = json_object_get(parent, TAG_HOSTNQN);
	if (obj) {
		if (formatted)
			printf(",\n  " FMT_STR, TAG_HOSTNQN,
			       json_string_value(obj));
		else
			printf(", %s '%s'", TAG_HOSTNQN,
			       json_string_value(obj));
	}

	list = json_object_get(parent, TAG_SHARED);
	if (list)
		show_allowed_list(list, formatted, TAG_SHARED);

	list = json_object_get(parent, TAG_RESTRICTED);
	if (list)
		show_allowed_list(list, formatted, TAG_RESTRICTED);

	printf("\n");

	if (formatted)
		printf("}\n");
}

void show_group_data(json_t *parent, int formatted)
{
	json_t			*obj;

	obj = json_object_get(parent, TAG_NAME);
	if (!obj) {
		printf("no %s name?\n", TAG_GROUP);
		return;
	}

	if (formatted)
		printf("{\n  " FMT_STR ",\n", TAG_NAME,
		       json_string_value(obj));
	else
		printf("%s '%s'\n", TAG_GROUP, json_string_value(obj));

	if (!formatted)
		printf("%s:\n", TAG_TARGETS);

	show_target_list(parent, formatted, 1);

	if (!formatted)
		printf("%s:\n", TAG_HOSTS);

	show_host_list(parent, formatted, 1);

	if (formatted)
		printf("}");

	printf("\n");
}

void show_config(json_t *parent, int formatted)
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

	if (formatted)
		printf("{\n  " FMT_TAG " [", TAG_INTERFACES);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_ID);
		if (!obj)
			continue;

		if (formatted)
			printf("%s{\n    " FMT_INT ",\n", (i) ? ", " : "",
			       TAG_ID, json_integer_value(obj));
		else
			printf("%s%lld: ", (i) ? "\n" : "",
			       json_integer_value(obj));

		obj = json_object_get(iter, TAG_TYPE);
		if (obj) {
			typ = json_string_value(obj);
			if (formatted)
				printf("    " FMT_STR ",\n", TAG_TYPE, typ);
			else
				printf("%s ", typ);
		}

		obj = json_object_get(iter, TAG_FAMILY);
		if (obj) {
			fam = json_string_value(obj);
			if (formatted)
				printf("    " FMT_STR ",\n", TAG_FAMILY, fam);
			else if (typ && strcmp(typ, fam))
				printf("%s ", fam);
		}

		obj = json_object_get(iter, TAG_ADDRESS);
		if (obj) {
			if (formatted)
				printf("    " FMT_STR ",\n", TAG_ADDRESS,
				       json_string_value(obj));
			else
				printf("%s", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_TRSVCID);
		if (obj) {
			if (formatted)
				printf("    " FMT_INT "\n  }",
				       TAG_TRSVCID,
				       json_integer_value(obj));
			else
				printf(":%lld", json_integer_value(obj));
		}
	}

	if (formatted)
		printf("]\n}");

	printf("\n");
	return;

err:
	if (formatted)
		printf("  " FMT_TAG " [ ]", TAG_INTERFACES);
	else
		printf("No Interfaces defined\n");
}

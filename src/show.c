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

#include <jansson.h>

#include "json.h"
#include "tags.h"
#include "show.h"

/* FMT_ON Schema implemented for standalone:
 *  {
 *    "Groups": [{
 *      "Name": "string",
 *	"Targets": [{
 *	    "Alias": "string",
 *	    "Refresh": "int",		N/A for RDS controlled network
 *	    "Ports": [{
 *		"PORTID": "int",
 *		"TRTYRE": "string",
 *		"TREQ": "int",
 *		"ADRFAM": "string",
 *		"TRADDR": "string",
 *		"TRSVCID": "int"
 *	    }],
 *	    "Subsystems": [{
 *		"SUBNQN": "string",
 *		"AllowAllHosts": "int",
 *		"Namespaces": [{	N/A for RDS controlled network
 *		    "NSID": "int",
 *		    "NSDEV": "string"
 *		}],
 *		"Hosts": [ "string" ]
 *	    }],
 *	    "NSDEV": [ "string" ]	N/A for RDS controlled network
 *	}],
 *	"Hosts": [{
 *	    "HOSTNQN": "string",
 *	    "Transport": [{
 *		"TRTYRE": "string",
 *		"ADRFAM": "string",
 *		"TRADDR": "string",
 *	    }]
 *	}]
 *    }]
 *  }
 */

#define FMT_TAG "\"%s\": "
#define FMT_STR FMT_TAG "\"%s\""
#define FMT_INT FMT_TAG "%lld"
#define FMT_VAL "\"%s\""

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

		obj = json_object_get(iter, TAG_NSDEV);
		if (obj) {
			if (formatted)
				printf(", " FMT_STR " ", TAG_NSDEV,
				       json_string_value(obj));
			else
				printf("'%s'", json_string_value(obj));
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
	json_t			*obj;
	char			 tab[] = "      ";
	int			 i, cnt;

	array = json_object_get(parent, TAG_HOSTS);

	cnt = (array) ? json_array_size(array) : 0;

	if (formatted)
		printf("      " FMT_TAG "[", TAG_HOSTS);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_HOSTNQN);
		if (!obj)
			continue;

		if (formatted)
			printf("%s\n%s  { " FMT_STR "}", i ? "," : "",
			       tab, TAG_HOSTNQN, json_string_value(obj));
		else
			printf("%s%s ", i ? ", " : tab,
			       json_string_value(obj));
	}

	if (formatted)
		printf("%s ],\n", cnt ? "\n      " : "");
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

		obj = json_object_get(iter, TAG_ALLOW_ALL);
		if (obj) {
			if (formatted)
				printf(", " FMT_INT ",\n",
				       TAG_ALLOW_ALL,
				       json_integer_value(obj));
			else if (json_integer_value(obj))
				printf("(allow_all_hosts)\n");
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
		printf("No Subsystems defined\n");
}

static void show_transport(json_t *iter, int formatted, int indent)
{
	json_t			*obj;
	char			 tab[8];

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
		if (formatted)
			printf(",\n%s" FMT_STR, tab,
			       TAG_TYPE, json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
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
		if (formatted)
			printf(",\n%s" FMT_STR, tab,
			       TAG_FAMILY, json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
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

	printf("\n");
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
		iter = json_array_get(array, i);
		show_transport(iter, formatted, 4);
	}

	if (formatted)
		printf("%s ],\n", cnt ? " " : "");
	else if (!cnt)
		printf("    No Port IDs defined\n");
}

static void show_devices(json_t *parent, int formatted)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	int			 i, cnt = 0;

	array = json_object_get(parent, TAG_NSDEVS);
	if (array)
		cnt = json_array_size(array);

	if (formatted)
		printf("  " FMT_TAG "[", TAG_NSDEVS);
	else
		printf("  %s\n    ", TAG_NSDEVS);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);
		obj = json_object_get(iter, TAG_NSDEV);
		if (!obj)
			continue;

		if (formatted)
			printf("%s " FMT_VAL, i ? "," : "",
			       json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	if (formatted)
		printf(" ],\n");
	else if (!cnt)
		printf("No NS Devices defined\n");
}

void show_target_data(json_t *parent, int formatted)
{
	json_t			*attrs;

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
		if (formatted)
			printf(",\n  " FMT_INT ",\n", TAG_REFRESH,
			       json_integer_value(attrs));
		else
			printf("%s %lld\n", TAG_REFRESH,
			       json_integer_value(attrs));
	}

	show_portids(parent, formatted);

	show_subsystems(parent, formatted);

	show_devices(parent, formatted);

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

static void show_interfaces(json_t *parent, int formatted)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	char			 tab[] = "      ";
	int			 i, cnt;

	array = json_object_get(parent, TAG_INTERFACES);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	if (formatted)
		printf("  " FMT_TAG "[", TAG_INTERFACES);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		if (formatted)
			printf("%s {\n", i ? "    }," : "");

		obj = json_object_get(iter, TAG_TYPE);
		if (obj) {
			if (formatted)
				printf("%s" FMT_STR ",\n", tab,
				       TAG_TYPE,
				       json_string_value(obj));
			else
				printf("%s  %s ", i ? "\n" : "",
				       json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_FAMILY);
		if (obj) {
			if (formatted)
				printf("%s" FMT_STR ",\n", tab,
				       TAG_FAMILY,
				       json_string_value(obj));
			else
				printf("%s ", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_ADDRESS);
		if (obj) {
			if (formatted)
				printf("%s" FMT_STR ",\n", tab,
				       TAG_ADDRESS,
				       json_string_value(obj));
			else
				printf("%s", json_string_value(obj));
		}
	}

	if (formatted)
		printf("    }\n  ]");

	return;

err:
	if (formatted)
		printf("  " FMT_TAG " [ ]", TAG_ACL);
}

void show_host_data(json_t *parent, int formatted)
{
	json_t			*obj;

	obj = json_object_get(parent, TAG_HOSTNQN);
	if (!obj)
		return;

	if (formatted)
		printf("{\n  " FMT_STR ",\n", TAG_HOSTNQN,
		       json_string_value(obj));
	else
		printf("%s '%s'\n", TAG_HOST, json_string_value(obj));

	show_interfaces(parent, formatted);

	if (formatted)
		printf("\n}");

	printf("\n");
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
			if (formatted)
				printf("    " FMT_STR ",\n", TAG_TYPE,
				       json_string_value(obj));
			else
				printf("%s ", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_FAMILY);
		if (obj) {
			if (formatted)
				printf("    " FMT_STR ",\n", TAG_FAMILY,
				       json_string_value(obj));
			else
				printf("%s ", json_string_value(obj));
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

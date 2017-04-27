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
//#include <json-c/json.h>
#include <jansson.h>

#include "json.h"
#include "tags.h"
#include "show.h"

/* JSON Schema implemented:
 *  {
 *	"Controllers": [{
 *	    "Alias": "string",
 *	    "Refresh": "int"
 *	    "Transport": {
 *		"Type": "string",
 *		"Family": "string",
 *		"Address": "string",
 *		"Port": "int"
 *	    }
 *	    "Subsystems": [{
 *		"NQN" : "string",
 *		"AllowAllHosts" : "int"
 *	    }]
 *	}],
 *	"Hosts": [{
 *	    "NQN": "string",
 *	    "ACL": [{
 *		"NQN" : "string",
 *		"Access" : "int"
 *	    }]
 *	}]
 *  }
 */

static int list_array(json_t *array, int formatted)
{
	json_t *obj;
	int i, cnt;

	cnt = json_array_size(array);
	if (!cnt)
		return -ENOENT;

	for (i = 0; i < cnt; i++) {
		obj = json_array_get(array, i);
		if (formatted)
			printf("%s\"%s\"", i ? ", " : "",
			       json_string_value(obj));
		else
			printf("%s%s", i ? "\n" : "",
			       json_string_value(obj));
	}

	if (!formatted)
		printf("\n");

	return 0;
}

void show_ctrl_list(json_t *parent, int formatted)
{
	json_t *array;

	if (formatted)
		printf("{ \"%s\": [ ", TAG_CTRLS);

	array = json_object_get(parent, TAG_CTRLS);
	if (!array)
		printf("No Controllers defined\n");
	else if (list_array(array, formatted) && !formatted)
		printf("No Controllers defined\n");
	else if (formatted)
		printf(" ] }\n");
}

static void show_subsys(json_t *parent, int formatted)
{
	json_t *array;
	json_t *iter;
	json_t *obj;
	int i, cnt;

	array = json_object_get(parent, TAG_SUBSYSTEMS);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	if (formatted)
		printf("  \"%s\": [", TAG_SUBSYSTEMS);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NQN);
		if (!obj)
			continue;

		if (formatted)
			printf("%s\n    { \"%s\": \"%s\"", i ? "," : "",
			       TAG_NQN, json_string_value(obj));
		else
			printf("%s%s ", i ? ", " : "",
			       json_string_value(obj));

		obj = json_object_get(iter, TAG_ALLOW_ALL);
		if (obj) {
			if (formatted)
				printf(", \"%s\": %lld", TAG_ALLOW_ALL,
				       json_integer_value(obj));
			else if (json_integer_value(obj))
				printf("(allow_all_hosts)");
			else
				printf("(restricted)");
		}

		if (formatted)
			printf(" }");
	}

	if (formatted)
		printf("\n  ]\n");

	return;
err:
	if (formatted)
		printf("  \"%s\" : [ ]\n", TAG_SUBSYSTEMS);
}

static void show_transport(json_t *parent, int formatted)
{
	json_t *iter;
	json_t *obj;

	iter = json_object_get(parent, TAG_TRANSPORT);
	if (!iter)
		goto err;

	if (formatted)
		printf("  \"%s\": {\n", TAG_TRANSPORT);

	obj = json_object_get(iter, TAG_TYPE);
	if (obj) {
		if (formatted)
			printf("    \"%s\": \"%s\"", TAG_TYPE,
			       json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_FAMILY);
	if (obj) {
		if (formatted)
			printf(",\n    \"%s\": \"%s\"", TAG_FAMILY,
			       json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_ADDRESS);
	if (obj) {
		if (formatted)
			printf(",\n    \"%s\": \"%s\"", TAG_ADDRESS,
			       json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_PORT);
	if (obj) {
		if (formatted)
			printf(",\n    \"%s\": %lld", TAG_PORT,
			       json_integer_value(obj));
		else
			printf("%lld ", json_integer_value(obj));
	}
	if (formatted)
		printf("\n  },");

	printf("\n");

	return;
err:
	if (formatted)
		printf("  \"%s\" : { },\n", TAG_TRANSPORT);
}

void show_ctrl_data(json_t *parent, int formatted)
{
	json_t *ctrl;
	json_t *attrs;

	ctrl = json_object_get(parent, TAG_CTRLS);
	if (!ctrl)
		return;

	attrs = json_object_get(ctrl, TAG_ALIAS);
	if (!attrs)
		return;

	if (formatted)
		printf("{\n  \"%s\": %s", TAG_ALIAS,
		       json_string_value(attrs));
	else
		printf("Controller %s ", json_string_value(attrs));

	attrs = json_object_get(ctrl, TAG_REFRESH);
	if (attrs) {
		if (formatted)
			printf(",\n  \"%s\": %lld,\n", TAG_REFRESH,
			       json_integer_value(attrs));
		else
			printf("%s %lld\n", TAG_REFRESH,
			       json_integer_value(attrs));
	}

	show_transport(ctrl, formatted);

	show_subsys(ctrl, formatted);

	if (formatted)
		printf("}");

	printf("\n");
}

void show_host_list(json_t *parent, int formatted)
{
	json_t *array;

	if (formatted)
		printf("{ \"%s\": [ ", TAG_HOSTS);

	array = json_object_get(parent, TAG_HOSTS);
	if (!array)
		printf("No Hosts defined\n");
	else if (list_array(array, formatted) && !formatted)
		printf("No Hosts defined\n");
	else if (formatted)
		printf(" ] }\n");
}

static void show_acl(json_t *parent, int formatted)
{
	json_t *array;
	json_t *iter;
	json_t *obj;
	int i, n, cnt;

	array = json_object_get(parent, TAG_ACL);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	if (formatted)
		printf("  \"%s\": [", TAG_ACL);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NQN);
		if (!obj)
			continue;

		if (formatted)
			printf("%s\n    { \"%s\": \"%s\"", i ? "," : "",
			       TAG_NQN, json_string_value(obj));
		else
			printf("%s%s ", i ? ", " : "",
			       json_string_value(obj));

		obj = json_object_get(iter, TAG_ACCESS);
		if (obj) {
			n = json_integer_value(obj);
			if (formatted)
				printf(", \"%s\": %d", TAG_ACCESS, n);
			else if (n == 0)
				printf("(No Access)");
			else if (n == 1)
				printf("(Read Only)");
			else if (n == 2)
				printf("(Write Only)");
			else if (n == 3)
				printf("(Read/Write)");
			else
				printf("(Unknown Access %d) ", n);
		}
		if (formatted)
			printf(" }");
	}

	if (formatted)
		printf("\n  ]");

	return;

err:
	if (formatted)
		printf("  \"%s\" : [ ]", TAG_ACL);
}

void show_host_data(json_t *parent, int formatted)
{
	json_t *host;
	json_t *obj;

	host = json_object_get(parent, TAG_HOSTS);
	if (!host)
		return;

	obj = json_object_get(host, TAG_NQN);
	if (!obj)
		return;

	if (formatted)
		printf("{\n  \"%s\": \"%s\",\n", TAG_NQN,
		       json_string_value(obj));
	else
		printf("Host %s\n", json_string_value(obj));

	show_acl(host, formatted);

	if (formatted)
		printf("\n}");

	printf("\n");
}

void show_config(json_t *parent, int formatted)
{
	json_t *array;
	json_t *iter;
	json_t *obj;
	int i, cnt;

	array = json_object_get(parent, TAG_INTERFACES);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	if (formatted)
		printf("{\n  \"%s\": [", TAG_INTERFACES);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_ID);
		if (!obj)
			continue;

		if (formatted)
			printf("%s{\n    \"%s\": %lld,\n", (i) ? ", " : "",
			       TAG_ID, json_integer_value(obj));
		else
			printf("%s%lld: ", (i) ? "\n" : "",
			       json_integer_value(obj));

		obj = json_object_get(iter, TAG_TYPE);
		if (obj) {
			if (formatted)
				printf("    \"%s\": \"%s\",\n", TAG_TYPE,
				       json_string_value(obj));
			else
				printf("%s ", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_FAMILY);
		if (obj) {
			if (formatted)
				printf("    \"%s\": \"%s\",\n", TAG_FAMILY,
				       json_string_value(obj));
			else
				printf("%s ", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_ADDRESS);
		if (obj) {
			if (formatted)
				printf("    \"%s\": \"%s\",\n", TAG_ADDRESS,
				       json_string_value(obj));
			else
				printf("%s", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_PORT);
		if (obj) {
			if (formatted)
				printf("    \"%s\": %lld\n  }", TAG_PORT,
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
		printf("  \"%s\" : [ ]", TAG_INTERFACES);
	else
		printf("No Interfaces defined\n");
}

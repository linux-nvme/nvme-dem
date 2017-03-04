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
#include <json-c/json.h>

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

static int list_array(struct json_object *array, const char *tag, int formatted)
{
	struct json_object *obj;
	int i, n, cnt;

	cnt = json_object_array_length(array);
	if (!cnt)
		return -ENOENT;

	for (i = 0; i < cnt; i++) {
		obj = json_object_array_get_idx(array, i);
		if (formatted)
			printf("%s\"%s\"", i ? ", " : "",
			       json_object_get_string(obj));
		else
			printf("%s%s", i ? "\n" : "",
			       json_object_get_string(obj));
	}

	if (!formatted)
		printf("\n");

	return 0;
}

void show_ctrl_list(struct json_object *parent, int formatted)
{
	struct json_object *array;

	if (formatted)
		printf("{ \"%s\": [ ", TAG_CTRLS);

	json_object_object_get_ex(parent, TAG_CTRLS, &array);
	if (!array)
		printf("No Controllers defined\n");
	else if (list_array(array, TAG_ALIAS, formatted) && !formatted)
		printf("No Controllers defined\n");
	else if (formatted)
		printf(" ] }\n");
}

static int show_subsys(struct json_object *parent, int formatted)
{
	struct json_object *array;
	struct json_object *iter;
	struct json_object *obj;
	int i, n, cnt;

	json_object_object_get_ex(parent, TAG_SUBSYSTEMS, &array);
	if (!array)
		goto err;

	cnt = json_object_array_length(array);

	if (cnt == 0)
		goto err;

	if (formatted)
		printf("  \"%s\":\n  [", TAG_SUBSYSTEMS);

	for (i = 0; i < cnt; i++) {
		iter = json_object_array_get_idx(array, i);

		json_object_object_get_ex(iter, TAG_NQN, &obj);
		if (!obj)
			continue;

		if (formatted)
			printf("%s\n    { \"%s\": \"%s\"", i ? "," : "",
			       TAG_NQN, json_object_get_string(obj));
		else
			printf("%s%s ", i ? ", " : "",
			       json_object_get_string(obj));

		json_object_object_get_ex(iter, TAG_ALLOW_ALL, &obj);
		if (obj) {
			if (formatted)
				printf(", \"%s\": %d", TAG_ALLOW_ALL,
				       json_object_get_int(obj));
			else if (json_object_get_int(obj))
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

static void show_transport(struct json_object *parent, int formatted)
{
	struct json_object *iter;
	struct json_object *obj;
	int n;

	json_object_object_get_ex(parent, TAG_TRANSPORT, &iter);
	if (!iter)
		goto err;

	if (formatted)
		printf("  \"%s\":\n  {\n", TAG_TRANSPORT);

	json_object_object_get_ex(iter, TAG_TYPE, &obj);
	if (obj) {
		if (formatted)
			printf("    \"%s\": \"%s\"", TAG_TYPE,
			       json_object_get_string(obj));
		else
			printf("%s ", json_object_get_string(obj));
	}

	json_object_object_get_ex(iter, TAG_FAMILY, &obj);
	if (obj) {
		if (formatted)
			printf(",\n    \"%s\": \"%s\"", TAG_FAMILY,
			       json_object_get_string(obj));
		else
			printf("%s ", json_object_get_string(obj));
	}

	json_object_object_get_ex(iter, TAG_ADDRESS, &obj);
	if (obj) {
		if (formatted)
			printf(",\n    \"%s\": \"%s\"", TAG_ADDRESS,
			       json_object_get_string(obj));
		else
			printf("%s ", json_object_get_string(obj));
	}

	json_object_object_get_ex(iter, TAG_PORT, &obj);
	if (obj) {
		if (formatted)
			printf(",\n    \"%s\": %d", TAG_PORT,
			       json_object_get_int(obj));
		else
			printf("%d ", json_object_get_int(obj));
	}
	if (formatted)
		printf("\n  },");

	printf("\n");

	return;
err:
	if (formatted)
		printf("  \"%s\" : { },\n", TAG_TRANSPORT);
}

void show_ctrl_data(struct json_object *parent, int formatted)
{
	struct json_object *ctrl;
	struct json_object *attrs;
	int i;

	json_object_object_get_ex(parent, TAG_CTRLS, &ctrl);
	if (!ctrl)
		return;

	json_object_object_get_ex(ctrl, TAG_ALIAS, &attrs);
	if (!attrs)
		return;

	if (formatted)
		printf("{\n  \"%s\": %s", TAG_ALIAS,
		       json_object_get_string(attrs));
	else
		printf("Controller %s ", json_object_get_string(attrs));

	json_object_object_get_ex(ctrl, TAG_REFRESH, &attrs);
	if (attrs) {
		if (formatted)
			printf(",\n  \"%s\": %d,\n", TAG_REFRESH,
			       json_object_get_int(attrs));
		else
			printf("%s %d\n", TAG_REFRESH,
			       json_object_get_int(attrs));
	}

	show_transport(ctrl, formatted);

	show_subsys(ctrl, formatted);

	if (formatted)
		printf("}");

	printf("\n");
}

void show_host_list(struct json_object *parent, int formatted)
{
	struct json_object *array;

	if (formatted)
		printf("{ \"%s\": [ ", TAG_HOSTS);

	json_object_object_get_ex(parent, TAG_HOSTS, &array);
	if (!array)
		printf("No Hosts defined\n");
	else if (list_array(array, TAG_ALIAS, formatted) && !formatted)
		printf("No Hosts defined\n");
	else if (formatted)
		printf(" ] }\n");
}

static int show_acl(struct json_object *parent, int formatted)
{
	struct json_object *array;
	struct json_object *iter;
	struct json_object *obj;
	int i, n, cnt;

	json_object_object_get_ex(parent, TAG_ACL, &array);
	if (!array)
		goto err;

	cnt = json_object_array_length(array);

	if (cnt == 0)
		goto err;

	if (formatted)
		printf("  \"%s\":\n  [", TAG_ACL);

	for (i = 0; i < cnt; i++) {
		iter = json_object_array_get_idx(array, i);

		json_object_object_get_ex(iter, TAG_NQN, &obj);
		if (!obj)
			continue;

		if (formatted)
			printf("%s\n    { \"%s\": \"%s\"", i ? "," : "",
			       TAG_NQN, json_object_get_string(obj));
		else
			printf("%s%s ", i ? ", " : "",
			       json_object_get_string(obj));

		json_object_object_get_ex(iter, TAG_ACCESS, &obj);
		if (obj) {
			n = json_object_get_int(obj);
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

void show_host_data(struct json_object *parent, int formatted)
{
	struct json_object *host;
	struct json_object *obj;

	json_object_object_get_ex(parent, TAG_HOSTS, &host);
	if (!host)
		return;

	json_object_object_get_ex(host, TAG_NQN, &obj);
	if (!obj)
		return;

	if (formatted)
		printf("{ \"%s\": {\n  \"%s\": \"%s\",\n", TAG_HOSTS, TAG_NQN,
		       json_object_get_string(obj));
	else
		printf("Host %s\n", json_object_get_string(obj)); 

	show_acl(host, formatted);

	if (formatted)
		printf("}}");

	printf("\n");
}

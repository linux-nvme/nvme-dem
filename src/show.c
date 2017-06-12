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

/* JSON Schema implemented:
 *  {
 *    "Groups": [{
 *      "Name": "string",
 *	"Controllers": [{
 *	    "Alias": "string",
 *	    "Certificate": "string",
 *	    "Refresh": "int"
 *	    "Transports": [{
 *		"Type": "string",
 *		"Family": "string",
 *		"Address": "string",
 *		"Port": "int"
 *	    }],
 *	    "Subsystems": [{
 *		"NQN": "string",
 *		"AllowAllHosts": "int"
 *	    }]
 *	}],
 *	"Hosts": [{
 *	    "NQN": "string",
 *	    "Certificate": "string",
 *	    "DomainUniqueNQN": "string",
 *	    "ACL": [{
 *		"NQN": "string",
 *		"Access": "int"
 *	    }]
 *	}]
 *    }]
 *  }
 */

#define JSTAG "\"%s\": "
#define JSSTR JSTAG "\"%s\""
#define JSINT JSTAG "%lld"
#define JSVAL "\"%s\""

static int list_array(json_t *array, int formatted, int indent)
{
	json_t			*obj;
	int			 i, cnt;

	cnt = json_array_size(array);
	if (!cnt)
		return -ENOENT;

	for (i = 0; i < cnt; i++) {
		obj = json_array_get(array, i);
		if (formatted)
			printf("%s" JSVAL, i ? ", " : "",
			       json_string_value(obj));
		else
			printf("%s%s%s", i ? "\n" : "", indent ? "  " : "",
			       json_string_value(obj));
	}

	if (!formatted)
		printf("\n");

	return 0;
}

void show_group_list(json_t *parent, int formatted)
{
	json_t			*array;

	if (formatted)
		printf("{ " JSTAG "[ ", TAG_GROUPS);

	array = json_object_get(parent, TAG_GROUPS);
	if (!array)
		printf("No %s defined\n", TAG_GROUPS);
	else if (list_array(array, formatted, 0) && !formatted)
		printf("No %s defined\n", TAG_GROUPS);
	else if (formatted)
		printf(" ] }\n");
}

void show_ctlr_list(json_t *parent, int formatted, int indent)
{
	json_t			*array;

	if (formatted)
		printf("{ " JSTAG " [ ", TAG_CTLRS);

	array = json_object_get(parent, TAG_CTLRS);
	if (!array)
		printf("%sNo %s defined\n", indent ? "  " : "", TAG_CTLRS);
	else if (list_array(array, formatted, indent) && !formatted)
		printf("%sNo %s defined\n", indent ? "  " : "", TAG_CTLRS);
	else if (formatted)
		printf(" ] }\n");
}

static void show_subsys(json_t *parent, int formatted)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	int			 i, cnt;

	array = json_object_get(parent, TAG_SUBSYSTEMS);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	if (formatted)
		printf("  " JSTAG " [", TAG_SUBSYSTEMS);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NQN);
		if (!obj)
			continue;

		if (formatted)
			printf("%s\n    { " JSSTR, i ? "," : "",
			       TAG_NQN, json_string_value(obj));
		else
			printf("%s%s ", i ? ", " : "", json_string_value(obj));

		obj = json_object_get(iter, TAG_ALLOW_ALL);
		if (obj) {
			if (formatted)
				printf(", " JSINT, TAG_ALLOW_ALL,
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
		printf("  " JSTAG " [ ]\n", TAG_SUBSYSTEMS);
}

static void show_transport(json_t *parent, int formatted)
{
	json_t			*iter;
	json_t			*obj;

	iter = json_object_get(parent, TAG_TRANSPORT);
	if (!iter)
		goto err;

	if (formatted)
		printf("  \"%s\": {\n", TAG_TRANSPORT);

	obj = json_object_get(iter, TAG_TYPE);
	if (obj) {
		if (formatted)
			printf("    " JSSTR, TAG_TYPE,
			       json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_FAMILY);
	if (obj) {
		if (formatted)
			printf(",\n    " JSSTR, TAG_FAMILY,
			       json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_ADDRESS);
	if (obj) {
		if (formatted)
			printf(",\n    " JSSTR, TAG_ADDRESS,
			       json_string_value(obj));
		else
			printf("%s ", json_string_value(obj));
	}

	obj = json_object_get(iter, TAG_PORT);
	if (obj) {
		if (formatted)
			printf(",\n    " JSINT, TAG_PORT,
			       json_integer_value(obj));
		else
			printf("%lld", json_integer_value(obj));
	}
	if (formatted)
		printf("\n  },");

	printf("\n");

	return;
err:
	if (formatted)
		printf("  \"%s\" : { },\n", TAG_TRANSPORT);
}

void show_ctlr_data(json_t *parent, int formatted)
{
	json_t			*attrs;

	attrs = json_object_get(parent, TAG_ALIAS);
	if (!attrs)
		return;

	if (formatted)
		printf("{\n  \"%s\": %s", TAG_ALIAS, json_string_value(attrs));
	else
		printf("%s '%s' ", TAG_CTLR, json_string_value(attrs));

	attrs = json_object_get(parent, TAG_REFRESH);
	if (attrs) {
		if (formatted)
			printf(",\n  " JSINT ",\n", TAG_REFRESH,
			       json_integer_value(attrs));
		else
			printf("%s %lld\n", TAG_REFRESH,
			       json_integer_value(attrs));
	}

	show_transport(parent, formatted);

	show_subsys(parent, formatted);

	if (formatted)
		printf("}");

	printf("\n");
}

void show_host_list(json_t *parent, int formatted, int indent)
{
	json_t			*array;

	if (formatted)
		printf("{ " JSTAG " [ ", TAG_HOSTS);

	array = json_object_get(parent, TAG_HOSTS);
	if (!array)
		printf("%sNo %s defined\n", indent ? "  " : "", TAG_HOSTS);
	else if (list_array(array, formatted, indent) && !formatted)
		printf("%sNo %s defined\n", indent ? "  " : "", TAG_HOSTS);
	else if (formatted)
		printf(" ] }\n");
}

static void show_acl(json_t *parent, int formatted)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	int			 i, n, cnt;

	array = json_object_get(parent, TAG_ACL);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	if (formatted)
		printf("  " JSTAG " [", TAG_ACL);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NQN);
		if (!obj)
			continue;

		if (formatted)
			printf("%s\n    { " JSSTR, i ? "," : "",
			       TAG_NQN, json_string_value(obj));
		else
			printf("%s%s ", i ? ", " : "", json_string_value(obj));

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
		printf("  " JSTAG " [ ]", TAG_ACL);
}

void show_host_data(json_t *parent, int formatted)
{
	json_t			*obj;
	json_t			*attrs;

	obj = json_object_get(parent, TAG_NQN);
	if (!obj)
		return;

	if (formatted)
		printf("{\n  " JSSTR ",\n", TAG_NQN, json_string_value(obj));
	else
		printf("%s '%s'\n", TAG_HOST, json_string_value(obj));

	attrs = json_object_get(parent, TAG_CERT);
	if (attrs) {
		if (formatted)
			printf("  " JSSTR ",\n", TAG_CERT,
			       json_string_value(attrs));
		else
			printf("%s '%s'\n", TAG_CERT,
			       json_string_value(attrs));
	}

	attrs = json_object_get(parent, TAG_ALIAS_NQN);
	if (attrs) {
		if (formatted)
			printf("  " JSSTR ",\n", TAG_ALIAS_NQN,
			       json_string_value(attrs));
		else
			printf("%s '%s'\n", TAG_ALIAS_NQN,
			       json_string_value(attrs));
	}

	show_acl(parent, formatted);

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
		printf("{\n  " JSSTR ",\n", TAG_NAME,
		       json_string_value(obj));
	else
		printf("%s '%s'\n", TAG_GROUP, json_string_value(obj));

	if (!formatted)
		printf("%s:\n", TAG_CTLRS);

	show_ctlr_list(parent, formatted, 1);

	if (!formatted)
		printf("%s:\n", TAG_HOSTS);

	show_host_list(parent, formatted, 1);

	if (formatted)
		printf("\n}");

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
		printf("{\n  " JSTAG " [", TAG_INTERFACES);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_ID);
		if (!obj)
			continue;

		if (formatted)
			printf("%s{\n    " JSINT ",\n", (i) ? ", " : "",
			       TAG_ID, json_integer_value(obj));
		else
			printf("%s%lld: ", (i) ? "\n" : "",
			       json_integer_value(obj));

		obj = json_object_get(iter, TAG_TYPE);
		if (obj) {
			if (formatted)
				printf("    " JSSTR ",\n", TAG_TYPE,
				       json_string_value(obj));
			else
				printf("%s ", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_FAMILY);
		if (obj) {
			if (formatted)
				printf("    " JSSTR ",\n", TAG_FAMILY,
				       json_string_value(obj));
			else
				printf("%s ", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_ADDRESS);
		if (obj) {
			if (formatted)
				printf("    " JSSTR ",\n", TAG_ADDRESS,
				       json_string_value(obj));
			else
				printf("%s", json_string_value(obj));
		}

		obj = json_object_get(iter, TAG_PORT);
		if (obj) {
			if (formatted)
				printf("    " JSINT "\n  }", TAG_PORT,
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
		printf("  " JSTAG " [ ]", TAG_INTERFACES);
	else
		printf("No Interfaces defined\n");
}

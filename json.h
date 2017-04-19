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

#include <json-c/json.h>

void store_config_file(void *context);
int del_ctrl(void *context, char *p);
int list_ctrl(void *context, char *response);
int rename_ctrl(void *context, char *old, char *new);
int set_ctrl(void *context, char *alias, char *data);
int rename_subsys(void *context, char *alias, char *old, char *new);
int show_ctrl(void *context, char *alias, char *response);
int set_host(void *context, char *nqn);
int del_host(void *context, char *nqn);
int list_host(void *context, char *response);
int rename_host(void *context, char *old, char *new);
int show_host(void *context, char *nqn, char *response);
int set_subsys(void *context, char *alias, char *ss, char *p);
int del_subsys(void *context, char *alias, char *ss);
int set_acl(void *context, char *nqn, char *ss, char *p);
int del_acl(void *context, char *nqn,  char *ss);


/* JSON Schema implemented:
 *  {
 *	"Controllers": [{
 *	    "Alias": "string",
 *	    "Refresh": "int",
 *	    "Transport": {
 *		"Type": "string",
 *		"Family": "string",
 *		"Address": "string",
 *		"Port": "int"
 *	    },
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

struct json_context {
	struct json_object *root;
	struct json_object *ctrls;
	struct json_object *hosts;
	char filename[128];
};

#define json_add_string(x, y, z) \
	json_object_object_add(x, y, json_object_new_string(z))
#define json_add_int(x, y, z) \
	json_object_object_add(x, y, json_object_new_int(z))
#define json_set_string(x, y, z) do { \
	json_object_object_get_ex(x, y, &tmp); \
	if (tmp) json_object_set_string(tmp, z); \
	else json_add_string(x, y, z); \
	} while (0)
#define json_set_int(x, y, z) do {\
	json_object_object_get_ex(x, y, &tmp); \
	if (tmp) json_object_set_int(tmp, z); \
	else json_add_int(x, y, z); \
	} while (0)
#define json_get_subgroup(x, y, z) do { \
	    if (json_object_get_type(x) == json_type_object) { \
		json_object_object_get_ex(x, y, &z); \
		if (!z) { \
			z = json_object_new_object(); \
			json_object_object_add(x, y, z); \
		} else if ( json_object_get_type(z) != json_type_object ) \
			fprintf(stderr, "%s(%d) Bad Target expect object\n", \
				__func__, __LINE__); \
	  } else \
		fprintf(stderr, "%s(%d) Bad Group Object\n", \
			__func__, __LINE__); \
	} while (0)
#define json_get_array(x, y, z) do { \
	    if (json_object_get_type(x) == json_type_object) { \
		json_object_object_get_ex(x, y, &z); \
		if (!z) { \
			z = json_object_new_array(); \
			json_object_object_add(x, y, z); \
		} else if ( json_object_get_type(z) != json_type_array ) \
			fprintf(stderr, "%s(%d) Bad Target expect array\n", \
				__func__, __LINE__); \
	  } else \
		fprintf(stderr, "%s(%d) Bad Group Object\n", \
			__func__, __LINE__); \
	} while (0)
#define json_update_string(w, x, y, z) do { \
	  json_object_object_get_ex(x, y, &z); \
	  if (z) { \
		if (json_object_get_type(x) == json_type_object) \
			json_set_string(w, y, json_object_get_string(z)); \
		else \
			fprintf(stderr, "%s(%d) Bad type\n", \
			       __func__, __LINE__); \
	  } \
	} while (0)
#define json_update_int(w, x, y, z) do { \
	  json_object_object_get_ex(x, y, &z); \
	  if (z) { \
		if (json_object_get_type(x) == json_type_object) \
			json_set_int(w, y, json_object_get_int(z)); \
		else \
			fprintf(stderr, "%s(%d) Bad type\n", \
			       __func__, __LINE__); \
	  } \
	} while (0)

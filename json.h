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

//#include <json-c/json.h>
#include <jansson.h>

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
	pthread_spinlock_t	 lock;
	json_t			*root;
	json_t			*ctrls;
	json_t			*hosts;
	char			 filename[128];
};

#define json_set_string(x, y, z) \
	do { \
		tmp = json_object_get(x, y); \
		if (tmp) json_string_set(tmp, z); \
		else json_object_set_new(x, y, json_string(z)); \
	} while (0)
#define json_set_int(x, y, z) \
	do {\
		tmp = json_object_get(x, y); \
		if (tmp) json_integer_set(tmp, z); \
		else json_object_set_new(x, y, json_integer(z)); \
	} while (0)
#define json_get_subgroup(x, y, z) \
	do { \
		if (json_is_object(x)) { \
			z = json_object_get(x, y); \
			if (!z) {\
				z = json_object(); \
				json_object_set_new(x, y, z); \
			} else if (!json_is_object(z)) \
				fprintf(stderr, "%s(%d) Bad Target expect object\n", \
					__func__, __LINE__), z = NULL; \
		} else \
			fprintf(stderr, "%s(%d) Bad Group Object\n", \
				__func__, __LINE__), z = NULL; \
	} while (0)
#define json_get_array(x, y, z) \
	do { \
		if (json_is_object(x)) { \
			z = json_object_get(x, y); \
			if (!z) { \
				z = json_array(); \
				json_object_set_new(x, y, z); \
			} else if (!json_is_array(z)) \
				fprintf(stderr, "%s(%d) Bad Target expect array\n", \
					__func__, __LINE__), z = NULL; \
		} else \
			fprintf(stderr, "%s(%d) Bad Group Object\n", \
				__func__, __LINE__), z = NULL; \
	} while (0)
#define json_update_string(w, x, y, z) \
	do { \
		z = json_object_get(x, y); \
		if (z) { \
			if (json_is_object(w)) \
				json_set_string(w, y, json_string_value(z)); \
			else \
				fprintf(stderr, "%s(%d) Bad type\n", \
					__func__, __LINE__); \
		} \
	} while (0)
#define json_update_int(w, x, y, z) \
	do { \
		z = json_object_get(x, y); \
		if (z) { \
			if (json_is_object(w)) \
				json_set_int(w, y, json_integer_value(z)); \
			else \
				fprintf(stderr, "%s(%d) Bad type\n", \
					__func__, __LINE__); \
		} \
	} while (0)

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

#include <jansson.h>

void store_config_file(void *ctx);

int list_group(void *ctx, char *resp);
int show_group(void *ctx, char *grp, char *resp);
int add_to_groups(void *ctx, char *data, char *resp);
int add_a_group(void *ctx, char *grp, char *resp);
int update_group(void *ctx, char *grp, char *data, char *resp);
int del_group(void *ctx, char *grp, char *resp);

int add_to_ctlrs(void *ctx, char *grp, char *data, char *resp);
int add_a_ctlr(void *ctx, char *grp, char *alias, char *resp);
int update_ctlr(void *ctx, char *grp, char *alias, char *data, char *resp);
int list_ctlr(void *ctx, char *grp, char *resp);
int show_ctlr(void *ctx, char *grp, char *alias, char *resp);
int del_ctlr(void *ctx, char *grp, char *alias, char *resp);

int add_to_hosts(void *ctx, char *grp, char *data, char *resp);
int add_a_host(void *ctx, char *grp, char *nqn, char *resp);
int update_host(void *ctx, char *grp, char *nqn, char *data, char *resp);
int list_host(void *ctx, char *grp, char *resp);
int show_host(void *ctx, char *grp, char *nqn, char *resp);
int del_host(void *ctx, char *grp, char *nqn, char *resp);

int set_subsys(void *ctx, char *grp, char *alias, char *ss, char *data,
	       int create, char *resp);
int del_subsys(void *ctx, char *grp, char *alias, char *ss, char *resp);

int set_acl(void *ctx, char *grp, char *nqn, char *ss, char *data, char *resp);
int del_acl(void *ctx, char *grp, char *nqn,  char *ss, char *resp);

/* JSON Schema implemented:
 * {
 *    Groups : [{
 *      "Name" : "string";
 *	"Controllers": [{
 *	    "Alias": "string",
 *	    "Certificate": "string",
 *	    "Refresh": "int",
 *	    "Transports": {
 *		"Type": "string",
 *		"Family": "string",
 *		"Address": "string",
 *		"Port": "int"
 *	    },
 *	    "Subsystems": [{
 *		"NQN" : "string",
 *		"Drive" : "string",
 *		"AllowAllHosts" : "int"
 *	    }],
 *	    "Driver": [ "string" ]
 *	}],
 *	"Hosts": [{
 *	    "NQN": "string",
 *	    "DomainUniqueNQN": "string",
 *	    "Certificate": "string",
 *	    "ACL": [{
 *		"NQN" : "string",
 *		"Access" : "int"
 *	    }]
 *	}]
 *   }]
 * }
 */

#define MAX_STRING		128

struct json_context {
	pthread_spinlock_t	 lock;
	json_t			*root;
	char			 filename[128];
};

#define json_set_string(x, y, z) \
	do { \
		tmp = json_object_get(x, y); \
		if (tmp) \
			json_string_set(tmp, z); \
		else \
			json_object_set_new(x, y, json_string(z)); \
	} while (0)
#define json_set_int(x, y, z) \
	do {\
		tmp = json_object_get(x, y); \
		if (tmp) \
			json_integer_set(tmp, z); \
		else \
			json_object_set_new(x, y, json_integer(z)); \
	} while (0)
#define json_get_subgroup(x, y, z) \
	do { \
		if (json_is_object(x)) { \
			z = json_object_get(x, y); \
			if (!z) {\
				z = json_object(); \
				json_object_set_new(x, y, z); \
			} else if (!json_is_object(z)) \
				fprintf(stderr, \
					"%s(%d) Bad Target expect object\n", \
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
				fprintf(stderr, \
					"%s(%d) Bad Target expect array\n", \
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

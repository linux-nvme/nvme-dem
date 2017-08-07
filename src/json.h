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

int add_to_targets(void *ctx, char *grp, char *data, char *resp);
int add_a_target(void *ctx, char *grp, char *alias, char *resp);
int update_target(void *ctx, char *grp, char *alias, char *data, char *resp);
int list_target(void *ctx, char *grp, char *resp);
int show_target(void *ctx, char *grp, char *alias, char *resp);
int del_target(void *ctx, char *grp, char *alias, char *resp);

int add_to_hosts(void *ctx, char *grp, char *data, char *resp);
int add_a_host(void *ctx, char *grp, char *nqn, char *resp);
int update_host(void *ctx, char *grp, char *nqn, char *data, char *resp);
int list_host(void *ctx, char *grp, char *resp);
int show_host(void *ctx, char *grp, char *nqn, char *resp);
int del_host(void *ctx, char *grp, char *nqn, char *resp);
int set_interface(void *ctx, char *group, char *host, char *data, char *resp);
int del_interface(void *ctx, char *group, char *host, char *data, char *resp);

int set_subsys(void *ctx, char *grp, char *alias, char *ss, char *data,
	       int create, char *resp);
int del_subsys(void *ctx, char *grp, char *alias, char *ss, char *resp);

int set_drive(void *ctx, char *grp, char *alias, char *data, char *resp);
int del_drive(void *ctx, char *grp, char *alias,  char *data, char *resp);

int set_portid(void *ctx, char *grp, char *alias, int portid, char *data,
	       char *resp);
int del_portid(void *ctx, char *grp, char *alias, int portid, char *resp);

int set_ns(void *ctx, char *grp, char *alias, char *ss, char *data,
	   char *resp);
int del_ns(void *ctx, char *grp, char *alias,  char *ss, int ns,
	   char *resp);

int set_acl(void *ctx, char *grp, char *alias, char *ss, char *hostnqn,
	    char *resp);
int del_acl(void *ctx, char *grp, char *alias,  char *ss, char *hostnqn,
	    char *resp);

/* JSON Schema implemented:
 * {
 *   Groups : [{
 *     "Name" : "string";
 *     "Targets": [{
 *       "Alias": "string", "Refresh": "integer",
 *       "PortIDs": [{
 *	   "PORTID": "integer" "TRTYPE": "string",
 *	   "ADRFAM": "string", "TREQ": "integer",
 *	   "TRADDR": "string", "TRSVCID": "integer"
 *       }],
 *       "Subsystems": [{
 *	   "SUBNQN" : "string", "AllowAllHosts" : "boolean"
 *	   "NSIDs": [{ "NSID": "integer", "NSDEV": "string" }]
 *	   "Hosts": [{ "HOSTNQN": "string" }]
 *       }],
 *       "NSDEV": [{ "NSDEV": "string" }]
 *     }],
 *     "Hosts": [{
 *       "HOSTNQN": "string",
 *       "Interfaces": [{
 *	   "TRTYPE": "string", "ADRFAM": "string", "TRADDR": "string",
 *       }]
 *     }]
 *   }]
 * }
 */

#define MAX_STRING		128

struct json_context {
	pthread_spinlock_t	 lock;
	json_t			*root;
	char			 filename[128];
};

/* json parsing helpers */

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
				json_set_string(w, y, \
						json_string_value(z)); \
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
				json_set_int(w, y, \
					     json_integer_value(z)); \
			else \
				fprintf(stderr, "%s(%d) Bad type\n", \
					__func__, __LINE__); \
		} \
	} while (0)

/* json output helpers */

#define JSARRAY		"\"%s\":["
#define JSEMPTYARRAY	"\"%s\":[]"
#define JSSTR		"\"%s\":\"%s\""
#define JSINT		"\"%s\":%lld"

#define array_json_string(obj, p, n)				\
	do {							\
		n = sprintf(p, "%s\"%s\"", i ? "," : "",	\
			    json_string_value(obj));		\
		p += n;						\
	} while (0)
#define start_json_array(tag, p, n)				\
	do {							\
		n = sprintf(p, JSARRAY, tag);			\
		p += n;						\
	} while (0)
#define end_json_array(p, n)					\
	do {							\
		n = sprintf(p, "]");				\
		p += n;						\
	} while (0)

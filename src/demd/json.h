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
int add_group(void *ctx, char *grp, char *resp);
int update_group(void *ctx, char *grp, char *data, char *resp);
int set_group_target(void *ctx, char *host, char *data, char *resp);
int set_group_host(void *ctx, char *host, char *data, char *resp);
int del_group(void *ctx, char *grp, char *resp);
int set_group_member(void *context, char *group, char *data, char *tag,
		     char *parent_tag, char *resp);
int del_group_member(void *context, char *group, char *member, char *tag,
		     char *parent_tag, char *resp);

int add_target(void *ctx, char *alias, char *resp);
int update_target(void *ctx, char *target, char *data, char *resp);
int list_target(void *ctx, char *resp);
int show_target(void *ctx, char *alias, char *resp);
int del_target(void *ctx, char *alias, char *resp);

int add_host(void *ctx, char *host, char *resp);
int update_host(void *context, char *host, char *data, char *resp);
int list_host(void *ctx, char *resp);
int show_host(void *ctx, char *alias, char *resp);
int del_host(void *ctx, char *host, char *resp);
int set_transport(void *ctx, char *host, char *data, char *resp);
int del_transport(void *ctx, char *host, char *data, char *resp);

int set_subsys(void *ctx, char *alias, char *ss, char *data, char *resp);
int del_subsys(void *ctx, char *alias, char *ss, char *resp);

int set_drive(void *ctx, char *alias, char *data, char *resp);
int del_drive(void *ctx, char *alias,  char *data, char *resp);

int set_interface(void *ctx, char *target, char *data, char *resp);
int set_portid(void *ctx, char *target, int portid, char *data,
	       char *resp);
int del_portid(void *ctx, char *alias, int portid, char *resp);

int set_ns(void *ctx, char *alias, char *ss, char *data,
	   char *resp);
int del_ns(void *ctx, char *alias,  char *ss, int ns,
	   char *resp);

int set_acl(void *ctx, char *alias, char *ss, char *hostnqn,
	    char *data, char *resp);
int del_acl(void *ctx, char *alias,  char *ss, char *hostnqn,
	    char *resp);

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

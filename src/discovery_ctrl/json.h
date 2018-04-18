/*
 * NVMe over Fabrics Distributed Endpoint Management (NVMe-oF DEM).
 * Copyright (c) 2017-2018 Intel Corporation, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <jansson.h>

struct target;
struct subsystem;
struct ns;
struct portid;
struct host;
union sc_iface;

struct json_context *get_json_context(void);
void store_json_config_file(void);

int list_json_group(char **resp);
int show_json_group(char *grp, char **resp);
int add_json_group(char *grp, char *resp);
int update_json_group(char *grp, char *data, char *resp, char *new_name);
int set_json_group_target(char *alias, char *data, char *resp);
int set_json_group_host(char *host, char *data, char *resp);
int del_json_group(char *grp, char *resp);
int set_json_group_member(char *group, char *data, char *alias, char *tag,
			  char *parent_tag, char *resp, char *_alias);
int del_json_group_member(char *group, char *member, char *tag,
			  char *parent_tag, char *resp);

int add_json_target(char *alias, char *resp);
int update_json_target(char *alias, char *data, char *resp,
		       struct target *target);
int list_json_target(char *query, char **resp);
int show_json_target(char *alias, char **resp);
int del_json_target(char *alias, char *resp);

int add_json_host(char *alias, char *resp);
int update_json_host(char *alias, char *data, char *resp,
		     char *newalias, char *nqn);
int list_json_host(char **resp);
int show_json_host(char *alias, char **resp);
int del_json_host(char *alias, char *resp, char *nqn);
int get_json_host_nqn(char *host, char *nqn);

int set_json_subsys(char *alias, char *subnqn, char *data, char *resp,
		    struct subsystem *subsys);
int del_json_subsys(char *alias, char *subnqn, char *resp);

int set_json_inb_interface(char *target, char *data, char *resp,
			   union sc_iface *face);
int set_json_oob_interface(char *target, char *data, char *resp,
			   union sc_iface *face);
int set_json_portid(char *target, int id, char *data, char *resp,
		    struct portid *portid);
int del_json_portid(char *alias, int id, char *resp);

int set_json_ns(char *alias, char *subnqn, char *data, char *resp,
		struct ns *ns);
int del_json_ns(char *alias,  char *subnqn, int ns, char *resp);
int set_json_acl(char *tgt, char *subnqn, char *alias, char *data,
		 char *resp, char *newalias, char *hostnqn);
int del_json_acl(char *alias,  char *subnqn, char *host, char *resp);

int list_group(char *resp);
int show_group(char *grp, char *resp);
int add_group(char *grp, char *resp);
int update_group(char *grp, char *data, char *resp);
int set_group_target(char *alias, char *data, char *resp);
int set_group_host(char *host, char *data, char *resp);
int del_group(char *grp, char *resp);
int set_group_member(char *group, char *data, char *alias, char *tag,
		     char *parent_tag, char *resp);
int del_group_member(char *group, char *member, char *tag, char *parent_tag,
		     char *resp);

int add_target(char *alias, char *resp);
int update_target(char *target, char *data, char *resp);
int list_target(char *query, char *resp);
int show_target(char *alias, char *resp);
int del_target(char *alias, char *resp);

int add_host(char *host, char *resp);
int update_host(char *host, char *data, char *resp);
int list_host(char *resp);
int show_host(char *host, char *resp);
int del_host(char *host, char *resp);

int set_subsys(char *alias, char *subnqn, char *data, char *resp);
int del_subsys(char *alias, char *subnqn, char *resp);

int set_interface(char *target, char *data, char *resp);
int set_portid(char *target, int portid, char *data,
	       char *resp);
int del_portid(char *alias, int portid, char *resp);

int set_ns(char *alias, char *subnqn, char *data, char *resp);
int del_ns(char *alias,  char *subnqn, int ns, char *resp);

int link_host(char *alias, char *subnqn, char *host, char *data, char *resp);
int unlink_host(char *alias,  char *subnqn, char *host, char *resp);

int update_signature(char *data, char *resp);

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
#define json_update_string_ex(w, x, y, z, res) \
	do { \
		z = json_object_get(x, y); \
		if (z) { \
			if (json_is_object(w)) { \
				json_set_string(w, y, \
						json_string_value(z)); \
				strcpy(res, (char *) json_string_value(z)); \
			} else \
				fprintf(stderr, "%s(%d) Bad type\n", \
					__func__, __LINE__); \
		} \
	} while (0)
#define json_update_int_ex(w, x, y, z, res) \
	do { \
		z = json_object_get(x, y); \
		if (z) { \
			if (json_is_object(w)) { \
				json_set_int(w, y, \
					     json_integer_value(z)); \
				res = json_integer_value(z); \
			} else \
				fprintf(stderr, "%s(%d) Bad type\n", \
					__func__, __LINE__); \
		} \
	} while (0)

/* json output helpers */

#define JSARRAY		"\"%s\":["
#define JSEMPTYARRAY	"\"%s\":[]"
#define JSSTR		"\"%s\":\"%s\""
#define JSINT		"\"%s\":%lld"

#define array_json_string(obj, p, i, n)				\
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

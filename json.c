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
	json_object_object_get_ex(x, y, &z); \
	if (!z) { \
		z = json_object_new_object(); \
		json_object_object_add(x, y, z); \
	  } \
	} while (0)
#define json_get_array(x, y, z) do { \
	json_object_object_get_ex(x, y, &z); \
	if (!z) { \
		z = json_object_new_array(); \
		json_object_object_add(x, y, z); \
	  } \
	} while (0)
#define json_update_string(w, x, y, z) do { \
		json_object_object_get_ex(x, y, &z); \
		if (z) json_set_string(w, y, json_object_get_string(z)); \
	} while (0)
#define json_update_int(w, x, y, z) do { \
		json_object_object_get_ex(x, y, &z); \
		if (z) json_set_int(w, y, json_object_get_int(z)); \
	} while (0)

/* helper functions */

static char *error(int ret)
{
	if (ret == -ENOENT)
		return "not found";
	if (ret == -EEXIST)
		return "already exists";
	return "unknown error";
}

static int find_array(struct json_object *array, const char *tag, char *val,
		      struct json_object **result)
{
	struct json_object *iter;
	struct json_object *obj;
	int i, n;

	n = json_object_array_length(array);
	for (i = 0; i < n; i++) {
		iter = json_object_array_get_idx(array, i);
		json_object_object_get_ex(iter, tag, &obj);
		if (obj && strcmp(json_object_get_string(obj), val) == 0) {
			if (result)
				*result = iter;
			return i;
		}
	}

	return -ENOENT;
}

static int list_array(struct json_object *array, char *tag, char *response)
{
	struct json_object *iter;
	struct json_object *obj;
	int i, n, cnt, total = 0;

	cnt = json_object_array_length(array);
	if (!cnt)
		return -ENOENT;

	for (i = 0; i < cnt; i++) {
		iter = json_object_array_get_idx(array, i);
		json_object_object_get_ex(iter, tag, &obj);
		n = sprintf(response, "%s\"%s\"", i ? "," : "",
			    json_object_get_string(obj));
		response += n;
		total += n;
	}

	return total;
}

static int del_from_array(struct json_object *parent, const char *tag,
			  char *value, const char *subgroup, char *ss)
{
	struct json_object *array;
	struct json_object *obj;
	int i;

	i = find_array(parent, tag, value, &obj);
	if (i < 0)
		goto err;

	json_object_object_get_ex(obj, subgroup, &array);
	if (!array)
		goto err;

	i = find_array(array, TAG_NQN, ss, NULL);
	if (i < 0)
		goto err;

	json_object_array_del_idx(array, i, 1);

	return 0;
err:
	return -ENOENT;
}

/* walk all host acls for ss name change */
static void rename_host_acl(struct json_object *hosts, char *old, char *new)
{
	struct json_object *array;
	struct json_object *iter;
	struct json_object *obj;
	struct json_object *tmp;
	int idx;
	int i;
	int cnt;

	cnt = json_object_array_length(hosts);

	for (i = 0; i < cnt; i++) {
		iter = json_object_array_get_idx(hosts, i);

		json_object_object_get_ex(iter, TAG_ACL, &array);
		if (!array)
			continue;

		idx = find_array(array, TAG_NQN, old, &obj);
		if (idx >= 0)
			json_set_string(obj, TAG_NQN, new);
	}
}

/* walk all host acls for ss deletion */
static void del_host_acl(struct json_object *hosts, char *nqn)
{
	struct json_object *array;
	struct json_object *obj;
	int idx;
	int i;
	int cnt;

	cnt = json_object_array_length(hosts);

	for (i = 0; i < cnt; i++) {
		obj = json_object_array_get_idx(hosts, i);

		json_object_object_get_ex(obj, TAG_ACL, &array);
		if (!array)
			continue;

		idx = find_array(array, TAG_NQN, nqn, NULL);
		if (idx >= 0)
			json_object_array_del_idx(array, idx, 1);
	}
}

static void parse_config_file(struct json_context *ctx)
{
	struct json_object *root;
	struct json_object *ctrls;
	struct json_object *hosts;
	int dirty = 0;

	root = json_object_from_file(ctx->filename);
	if (!root) {
		root = json_object_new_object();
		ctrls = json_object_new_array();
		json_object_object_add(root, TAG_CTRLS, ctrls);
		hosts = json_object_new_array();
		json_object_object_add(root, TAG_HOSTS, hosts);
		dirty = 1;
	} else {
		if (!json_object_object_get_ex(root, TAG_CTRLS, &ctrls)) {
			ctrls = json_object_new_array();
			json_object_object_add(root, TAG_CTRLS, ctrls);
			dirty = 1;
		}
		if (!json_object_object_get_ex(root, TAG_HOSTS, &hosts)) {
			hosts = json_object_new_array();
			json_object_object_add(root, TAG_HOSTS, hosts);
			dirty = 1;
		}
	}

	ctx->root = root;
	ctx->ctrls = ctrls;
	ctx->hosts = hosts;

	if (dirty)
		store_config_file(ctx);
}

/* command functions */

void store_config_file(void *context)
{
	struct json_context *ctx = context;
	struct json_object *root = ctx->root;
	char *filename = ctx->filename;
	int ret;

	ret = json_object_to_file_ext(filename, root,
				      JSON_C_TO_STRING_PRETTY |
				      JSON_C_TO_STRING_SPACED);
	if (ret)
		fprintf(stderr, "json_object_to failed %d\n", ret);
}

void *init_json(char *filename)
{
	struct json_context *ctx;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	strncpy(ctx->filename, filename, sizeof(ctx->filename));

	parse_config_file(ctx);

	return ctx;
}

void cleanup_json(void *context)
{
	struct json_context *ctx = context;

	json_object_put(ctx->ctrls);
	json_object_put(ctx->hosts);
	json_object_put(ctx->root);

	free(ctx);
}

int del_ctrl(void *context, char *alias)
{
	struct json_context *ctx = context;
	struct json_object *parent = ctx->ctrls;
	struct json_object *array;
	struct json_object *iter;
	struct json_object *obj;
	struct json_object *ss;
	int i, n, idx;

	idx = find_array(parent, TAG_ALIAS, alias, &iter);
	if (idx < 0)
		return -ENOENT;

	json_object_object_get_ex(iter, TAG_SUBSYSTEMS, &array);
	if (array) {
		n = json_object_array_length(array);

		for (i = 0; i < n; i++) {
			obj = json_object_array_get_idx(array, 0);
			json_object_object_get_ex(obj, TAG_NQN, &ss);
			del_subsys(ctx, alias,
				   (char *) json_object_get_string(ss));
		}
	}

	json_object_array_del_idx(parent, idx, 1);

	return 0;
}

int list_ctrl(void *context, char *response)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	int n;

	n = sprintf(response, "{\"%s\":[", TAG_CTRLS);
	response += n;

	n = list_array(array, TAG_ALIAS, response);
	if (n < 0)
		return n;

	response += n;

	sprintf(response, "]}");

	return 0;
}

int rename_ctrl(void *context, char *old, char *new)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *iter;
	struct json_object *tmp;
	int i;

	if (find_array(array, TAG_ALIAS, new, NULL) >= 0)
		return -EEXIST;

	i = find_array(array, TAG_ALIAS, old, &iter);
	if (i < 0)
		return -ENOENT;

	json_set_string(iter, TAG_ALIAS, new);

	return 0;
}

void add_transport(struct json_object *parent, struct json_object *new)
{
	struct json_object *subgroup;
	struct json_object *value;
	struct json_object *tmp;

	json_get_subgroup(parent, TAG_TRANSPORT, subgroup);

	json_update_string(subgroup, new, TAG_TYPE, value);
	json_update_string(subgroup, new, TAG_FAMILY, value);
	json_update_string(subgroup, new, TAG_ADDRESS, value);
	json_update_int(subgroup, new, TAG_PORT, value);
}

void add_subsys(struct json_object *parent, struct json_object *newparent)
{
	struct json_object *subgroup;
	struct json_object *new;
	struct json_object *obj;
	struct json_object *value;
	struct json_object *tmp;
	int i, n;

	json_get_array(parent, TAG_SUBSYSTEMS, subgroup);

	json_object_object_get_ex(newparent, TAG_SUBSYSTEMS, &new);
	if (!new)
		return;

	n = json_object_array_length(new);

	for (i = 0; i < n; i++) {
		obj = json_object_array_get_idx(new, i);

		json_update_string(subgroup, obj, TAG_NQN, value);
		json_update_int(subgroup, new, TAG_ALLOW_ALL, value);
	}
}

int set_ctrl(void *context, char *alias, char *data)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *iter;
	struct json_object *new;
	struct json_object *value;
	struct json_object *tmp;
	int i;

	i = find_array(array, TAG_ALIAS, alias, &iter);
	if (i < 0) {
		iter = json_object_new_object();
		json_add_string(iter, TAG_ALIAS, alias);
		json_object_array_add(array, iter);
	}

	new = json_tokener_parse(data);
	if (!new)
		return -EINVAL;

	json_update_string(iter, new, TAG_ALIAS, value);
	json_update_int(iter, new, TAG_REFRESH, value);

	add_transport(iter, new);

	add_subsys(iter, new);

	return 0;
}

static int show_subsys(struct json_object *parent, char *response)
{
	struct json_object *array;
	struct json_object *iter;
	struct json_object *obj;
	char *p = response;
	int i, n, cnt;

	json_object_object_get_ex(parent, TAG_SUBSYSTEMS, &array);
	if (!array)
		goto err;

	cnt = json_object_array_length(array);

	if (cnt == 0)
		goto err;

	n = sprintf(p, "\"%s\":[", TAG_SUBSYSTEMS);
	p += n;

	for (i = 0; i < cnt; i++) {
		iter = json_object_array_get_idx(array, i);

		json_object_object_get_ex(iter, TAG_NQN, &obj);
		if (!obj)
			continue;

		n = sprintf(p, "%s{\"%s\":\"%s\"", i ? "," : "",
			    TAG_NQN, json_object_get_string(obj));
		p += n;

		json_object_object_get_ex(iter, TAG_ALLOW_ALL, &obj);
		if (obj) {
			n = sprintf(p, ",\"%s\":%d", TAG_ALLOW_ALL,
				    json_object_get_int(obj));
			p += n;
		}
		n = sprintf(p, "}");
		p += n;
	}

	n = sprintf(p, "]");
	p += n;

	return p - response;
err:
	return sprintf(response, "\"%s\":[]", TAG_SUBSYSTEMS);
}

static int show_transport(struct json_object *parent, char *response)
{
	struct json_object *iter;
	struct json_object *obj;
	char *p = response;
	int n;

	json_object_object_get_ex(parent, TAG_TRANSPORT, &iter);
	if (!iter)
		goto err;

	n = sprintf(p, "\"%s\":{", TAG_TRANSPORT);
	p += n;

	json_object_object_get_ex(iter, TAG_TYPE, &obj);
	if (obj) {
		n = sprintf(p, "\"%s\":\"%s\"", TAG_TYPE,
			    json_object_get_string(obj));
		p += n;
	}

	json_object_object_get_ex(iter, TAG_FAMILY, &obj);
	if (obj) {
		n = sprintf(p, ",\"%s\":\"%s\"", TAG_FAMILY,
			    json_object_get_string(obj));
		p += n;
	}

	json_object_object_get_ex(iter, TAG_ADDRESS, &obj);
	if (obj) {
		n = sprintf(p, ",\"%s\":\"%s\"", TAG_ADDRESS,
			    json_object_get_string(obj));
		p += n;
	}

	json_object_object_get_ex(iter, TAG_PORT, &obj);
	if (obj) {
		n = sprintf(p, ",\"%s\":%d", TAG_PORT,
			    json_object_get_int(obj));
		p += n;
	}
	n = sprintf(p, "},");
	p += n;

	return p - response;
err:
	return sprintf(response, "\"%s\":{},", TAG_TRANSPORT);
}

int show_ctrl(void *context, char *alias, char *response)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *obj;
	struct json_object *ctrl;

	int i;

	i = find_array(array, TAG_ALIAS, alias, &ctrl);
	if (i < 0)
		return -ENOENT;

	i = sprintf(response, "{\"%s\":{\"%s\":\"%s\"",
		    TAG_CTRLS, TAG_ALIAS, alias);
	response += i;

	json_object_object_get_ex(ctrl, TAG_REFRESH, &obj);
	if (obj) {
		i = sprintf(response, ",\"%s\":%d,", TAG_REFRESH,
			    json_object_get_int(obj));
		response += i;
	}

	i = show_transport(ctrl, response);
	response += i;

	i = show_subsys(ctrl, response);
	response += i;

	sprintf(response, "}}");

	return 0;
}

int set_host(void *context, char *nqn)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	struct json_object *iter;
	int i;

	i = find_array(array, TAG_NQN, nqn, &iter);
	if (i >= 0)
		return -EEXIST;

	iter = json_object_new_object();
	json_add_string(iter, TAG_NQN, nqn);
	json_object_array_add(array, iter);

	return 0;
}

int del_host(void *context, char *nqn)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	struct json_object *iter;
	int i;

	i = find_array(array, TAG_NQN, nqn, &iter);
	if (i < 0)
		return -ENOENT;

	json_object_array_del_idx(array, i, 1);

	return 0;
}

int list_host(void *context, char *response)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	int n;

	n = sprintf(response, "{\"%s\":[", TAG_HOSTS);
	response += n;

	n = list_array(array, TAG_NQN, response);
	if (n < 0)
		return n;

	response += n;

	sprintf(response, "]}");

	return 0;
}

int rename_host(void *context, char *old, char *new)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	struct json_object *iter;
	struct json_object *tmp;
	int i;

	if (find_array(array, TAG_NQN, new, NULL) >= 0)
		return -EEXIST;

	i = find_array(array, TAG_NQN, old, &iter);
	if (i < 0)
		return -ENOENT;

	json_set_string(iter, TAG_NQN, new);

	return 0;
}

static int show_acl(struct json_object *parent, char *response)
{
	struct json_object *array;
	struct json_object *iter;
	struct json_object *obj;
	char *p = response;
	int i, n, cnt;

	json_object_object_get_ex(parent, TAG_ACL, &array);
	if (!array)
		goto err;

	cnt = json_object_array_length(array);

	if (cnt == 0)
		goto err;

	n = sprintf(p, "\"%s\":[", TAG_ACL);
	p += n;

	for (i = 0; i < cnt; i++) {
		iter = json_object_array_get_idx(array, i);

		json_object_object_get_ex(iter, TAG_NQN, &obj);
		if (!obj)
			continue;

		n = sprintf(p, "%s{\"%s\":\"%s\"", i ? "," : "",
			    TAG_NQN, json_object_get_string(obj));
		p += n;

		json_object_object_get_ex(iter, TAG_ACCESS, &obj);
		if (obj) {
			n = sprintf(p, ",\"%s\":%d", TAG_ACCESS,
				    json_object_get_int(obj));
			p += n;
		}
		n = sprintf(p, "}");
		p += n;
	}

	n = sprintf(p, "]");
	p += n;

	return p - response;
err:
	return sprintf(response, "\"%s\":[]", TAG_ACL);
}

int show_host(void *context, char *nqn, char *response)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	struct json_object *obj;
	int i;

	i = find_array(array, TAG_NQN, nqn, &obj);
	if (i < 0)
		return -ENOENT;

	i = sprintf(response, "{\"%s\":{\"%s\":\"%s\",",
		    TAG_HOSTS, TAG_NQN, nqn);
	response += i;

	i = show_acl(obj, response);
	response += i;

	sprintf(response, "}}");

	return 0;
}

int set_subsys(void *context, char *alias, char *ss, char *data)
{
	struct json_context *ctx = context;
	struct json_object *parent = ctx->ctrls;
	struct json_object *obj;
	struct json_object *iter;
	struct json_object *array;
	struct json_object *new;
	struct json_object *value;
	struct json_object *tmp;
	int i;

	i = find_array(parent, TAG_ALIAS, alias, &obj);
	if (i < 0)
		return -ENOENT;

	json_get_array(obj, TAG_SUBSYSTEMS, array);

	i = find_array(array, TAG_NQN, ss, &iter);
	if (i < 0) {
		iter = json_object_new_object();
		json_add_string(iter, TAG_NQN, ss);
		json_object_array_add(array, iter);
	}

	new = json_tokener_parse(data);
	if (!new)
		return -EINVAL;

	json_update_string(iter, new, TAG_NQN, value);
	json_update_int(iter, new, TAG_ALLOW_ALL, value);

	return 0;
}

int del_subsys(void *context, char *alias, char *ss)
{
	struct json_context *ctx = context;
	struct json_object *parent = ctx->ctrls;
	int ret;

	ret = del_from_array(parent, TAG_ALIAS, alias, TAG_SUBSYSTEMS, ss);
	if (!ret)
		del_host_acl(ctx->hosts, ss);

	return ret;
}

int rename_subsys(void *context, char *alias, char *old, char *new)
{
	struct json_context *ctx = context;
	struct json_object *parent = ctx->ctrls;
	struct json_object *iter;
	struct json_object *array;
	struct json_object *obj;
	struct json_object *tmp;
	int i;
	int idx;

	i = find_array(parent, TAG_ALIAS, alias, &iter);
	if (i < 0)
		return -ENOENT;

	json_object_object_get_ex(iter, TAG_SUBSYSTEMS, &array);
	if (!array)
		return -ENOENT;

	idx = find_array(array, TAG_NQN, old, &obj);
	if (idx < 0)
		return -ENOENT;

	json_set_string(obj, TAG_NQN, new);

	rename_host_acl(ctx->hosts, old, new);

	return 0;
}

int set_acl(void *context, char *nqn, char *ss, char *data)
{
	struct json_context *ctx = context;
	struct json_object *parent = ctx->hosts;
	struct json_object *obj;
	struct json_object *iter;
	struct json_object *array;
	struct json_object *new;
	struct json_object *value;
	struct json_object *tmp;
	int i;

	i = find_array(parent, TAG_NQN, nqn, &obj);
	if (i < 0)
		return -ENOENT;

	json_get_array(obj, TAG_ACL, array);

	i = find_array(array, TAG_NQN, ss, &iter);
	if (i < 0) {
		iter = json_object_new_object();
		json_add_string(iter, TAG_NQN, ss);
		json_object_array_add(array, iter);
	}

	new = json_tokener_parse(data);
	if (!new)
		return -EINVAL;

	json_update_string(iter, new, TAG_NQN, value);
	json_update_int(iter, new, TAG_ACCESS, value);

	return 0;
}

int del_acl(void *context, char *nqn, char *ss)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;

	return del_from_array(array, TAG_NQN, nqn, TAG_ACL, ss);
}

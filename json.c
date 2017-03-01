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

/* JSON Schema implemented:
 *  {
 *	"Controllers": [{
 *	    "Alias": "string",
 *	    "Refresh": "int"
 *	    "Type": "string",
 *	    "Family": "string",
 *	    "Address": "string",
 *	    "Port": "int"
 *	    "Subsystems": [ "string" ]
 *	}],
 *	"Hosts": [{
 *	    "NQN": "string",
 *	    "ACL": [ "string" ]
 *	}]
 *  }
 */

struct json_context {
	struct json_object *root;
	struct json_object *ctrls;
	struct json_object *hosts;
	char filename[128];
};

static const char *tagCtrls		= "Controllers";
static const char *tagSubsystems	= "Subsystems";
static const char *tagHosts		= "Hosts";
static const char *tagNQN		= "NQN";
static const char *tagAlias		= "Alias";
static const char *tagType		= "Type";
static const char *tagFamily		= "Family";
static const char *tagAddress		= "Address";
static const char *tagPort		= "Port";
static const char *tagACL		= "ACL";
static const char *tagRefresh		= "Refresh";

#define json_add_string(x, y, z) \
	json_object_object_add(x, y, json_object_new_string(z))
#define json_add_int(x, y, z) \
	json_object_object_add(x, y, json_object_new_int(z))
#define json_set_string(w, x, y, z) do { \
	json_object_object_get_ex(w, y, &x); \
	json_object_set_string(x, z); \
	json_object_put(x); \
	} while (0)
#define json_set_int(w, x, y, z) do {\
	json_object_object_get_ex(w, y, &x); \
	json_object_set_int(x, z); \
	json_object_put(x); \
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

static int find_string(struct json_object *array, char *val)
{
	struct json_object *iter;
	int i, n;

	n = json_object_array_length(array);
	for (i = 0; i < n; i++) {
		iter = json_object_array_get_idx(array, i);
		if (iter && strcmp(json_object_get_string(iter), val) == 0)
			return i;
	}

	return -ENOENT;
}

static void show_list(struct json_object *parent, const char *tag,
			char *response)
{
	struct json_object *array;
	struct json_object *obj;
	int i, n, cnt;

	json_object_object_get_ex(parent, tag, &array);
	if (!array)
		return;

	cnt = json_object_array_length(array);

	if (cnt == 0) {
		fprintf(stderr, "no %s specified\n", tag);
		return;
	}

	n = sprintf(response, "%s List:", tag);
	response += n;

	for (i = 0; i < cnt; i++) {
		obj = json_object_array_get_idx(array, i);
		n = sprintf(response, "%s %s", i ? "," : "",
				json_object_get_string(obj));
		response += n;
	}

	strcpy(response, "\n");
}

static int list_array(struct json_object *array, const char *tag,
			char *response)
{
	struct json_object *iter;
	struct json_object *obj;
	int i, n, cnt;

	cnt = json_object_array_length(array);
	for (i = 0; i < cnt; i++) {
		iter = json_object_array_get_idx(array, i);
		json_object_object_get_ex(iter, tag, &obj);
		n = sprintf(response, "%s\r\n", json_object_get_string(obj));
		response += n;
	}

	return 0;
}

static int add_to_array(struct json_object *parent, const char *target,
			char *value, const char *tag, char *p)
{
	struct json_object *array;
	struct json_object *obj;
	int i;

	i = find_array(parent, target, value, &obj);
	if (i < 0)
		return -ENOENT;

	json_object_object_get_ex(obj, tag, &array);
	if (!array) {
		array = json_object_new_array();
		json_object_object_add(obj, tag, array);
	}

	i = find_string(array, p);
	if (i >= 0)
		fprintf(stderr, "%s already in %s\n", p, tag);
	else
		json_object_array_add(array,
				      json_object_new_string(p));

	return 0;
}

static int del_from_array(struct json_object *parent, const char *target,
			  char *value, const char *tag, char *p)
{
	struct json_object *array;
	struct json_object *obj;
	int i;

	i = find_array(parent, target, value, &obj);
	if (i < 0)
		return -ENOENT;

	json_object_object_get_ex(obj, tag, &array);
	if (!array)
		return 0;

	i = find_string(array, p);
	if (i < 0) {
		fprintf(stderr, "%s not found in %s\n", p, tag);
		return -ENOENT;
	}

	json_object_array_del_idx(array, i, 1);

	/* if array is empty, delete the json rapper */

	if (json_object_array_length(array) == 0)
		json_object_object_del(obj, tag);

	return 0;
}

static void rename_host_acl(struct json_object *hosts, char *old, char *new)
{
	struct json_object *array;
	struct json_object *obj;
	int idx;
	int i;
	int cnt;

	cnt = json_object_array_length(hosts);

	for (i = 0; i < cnt; i++) {
		obj = json_object_array_get_idx(hosts, i);

		json_object_object_get_ex(obj, tagACL, &array);
		if (!array)
			continue;

		idx = find_string(array, old);
		if (idx >= 0)
			json_object_array_put_idx(array, idx,
						  json_object_new_string(new));
	}
}

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

		json_object_object_get_ex(obj, tagACL, &array);
		if (!array)
			continue;

		idx = find_string(array, nqn);
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
		json_object_object_add(root, tagCtrls, ctrls);
		hosts = json_object_new_array();
		json_object_object_add(root, tagHosts, hosts);
		dirty = 1;
	} else {
		if (!json_object_object_get_ex(root, tagCtrls, &ctrls)) {
			ctrls = json_object_new_array();
			json_object_object_add(root, tagCtrls, ctrls);
			dirty = 1;
		}
		if (!json_object_object_get_ex(root, tagHosts, &hosts)) {
			hosts = json_object_new_array();
			json_object_object_add(root, tagHosts, hosts);
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
}

int add_ctrl(void *context, char *alias)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *attrs;

	if (find_array(array, tagAlias, alias, NULL) >= 0)
		return -EEXIST;

	attrs = json_object_new_object();

	json_add_string(attrs, tagAlias, alias);

	json_object_array_add(array, attrs);

	return 0;
}

int del_ctrl(void *context, char *alias)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *iter;
	int i;

	i = find_array(array, tagAlias, alias, &iter);
	if (i < 0)
		return -ENOENT;

	json_object_object_del(iter, tagAlias);
	json_object_object_del(iter, tagRefresh);

	json_object_object_del(iter, tagType);
	json_object_object_del(iter, tagFamily);
	json_object_object_del(iter, tagAddress);
	json_object_object_del(iter, tagPort);

	json_object_array_del_idx(array, i, 1);

	return 0;
}

int list_ctrl(void *context, char *response)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;

	return list_array(array, tagAlias, response);
}

int rename_ctrl(void *context, char *old, char *new)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *attrs;
	struct json_object *iter;
	int i;

	if (find_array(array, tagAlias, new, NULL) >= 0)
		return -EEXIST;

	i = find_array(array, tagAlias, old, &iter);
	if (i < 0)
		return -ENOENT;

	json_set_string(iter, attrs, tagAlias, new);

	return 0;
}

int set_ctrl(void *context, char *alias, char *type, char *family,
		char *address, int port, int refresh)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *attrs;
	struct json_object *iter;
	int i;

	i = find_array(array, tagAlias, alias, &iter);
	if (i < 0)
		return -ENOENT;

	json_set_string(iter, attrs, tagAlias, alias);
	json_set_int(iter, attrs, tagRefresh, refresh);

	json_set_string(iter, attrs, tagType, type);
	json_set_string(iter, attrs, tagFamily, family);
	json_set_string(iter, attrs, tagAddress, address);
	json_set_int(iter, attrs, tagPort, port);

	return 0;
}

int show_ctrl(void *context, char *alias, char *response)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	struct json_object *attrs;
	struct json_object *iter;
	int i;

	i = find_array(array, tagAlias, alias, &iter);
	if (i < 0)
		return -ENOENT;

	json_object_object_get_ex(iter, tagType, &attrs);
	i = sprintf(response, "%s '%s', ", tagType,
			json_object_get_string(attrs));
	response += i;

	json_object_object_get_ex(iter, tagFamily, &attrs);
	i = sprintf(response, "%s '%s', ", tagFamily,
			json_object_get_string(attrs));
	response += i;

	json_object_object_get_ex(iter, tagAddress, &attrs);
	i = sprintf(response, "%s '%s', ", tagAddress,
			json_object_get_string(attrs));
	response += i;

	json_object_object_get_ex(iter, tagPort, &attrs);
	i = sprintf(response, "%s %d, ", tagPort,
			json_object_get_int(attrs));
	response += i;

	json_object_object_get_ex(iter, tagRefresh, &attrs);
	i = sprintf(response, "%s %d\r\n", tagRefresh,
			json_object_get_int(attrs));
	response += i;

	show_list(iter, tagSubsystems, response);

	return 0;
}

int add_host(void *context, char *nqn)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	struct json_object *attrs;

	if (find_array(array, tagNQN, nqn, NULL) >= 0)
		return -EEXIST;

	attrs = json_object_new_object();

	json_add_string(attrs, tagNQN, nqn);

	json_object_array_add(array, attrs);

	return 0;
}

int del_host(void *context, char *nqn)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	struct json_object *iter;
	int i;

	i = find_array(array, tagNQN, nqn, &iter);
	if (i < 0)
		return -ENOENT;

	json_object_object_del(iter, tagNQN);

	json_object_array_del_idx(array, i, 1);

	return 0;
}

int list_host(void *context, char *response)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;

	return list_array(array, tagNQN, response);
}

int rename_host(void *context, char *old, char *new)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	struct json_object *attrs;
	struct json_object *iter;
	int i;

	if (find_array(array, tagNQN, new, NULL) >= 0)
		return -EEXIST;

	i = find_array(array, tagNQN, old, &iter);
	if (i < 0)
		return -ENOENT;

	json_set_string(iter, attrs, tagNQN, new);

	return 0;
}

int show_host(void *context, char *nqn, char *response)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;
	struct json_object *obj;
	int i;

	i = find_array(array, tagNQN, nqn, &obj);
	if (i < 0)
		return -ENOENT;

	show_list(obj, tagACL, response);

	return 0;
}

int add_subsys(void *context, char *alias, char *ss)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;

	return add_to_array(array, tagAlias, alias, tagSubsystems, ss);
}

int del_subsys(void *context, char *alias, char *ss)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->ctrls;
	int ret;

	ret = del_from_array(array, tagAlias, alias, tagSubsystems, ss);
	if (!ret)
		del_host_acl(ctx->hosts, ss);

	return 0;
}

int rename_subsys(void *context, char *alias, char *old, char *new)
{
	struct json_context *ctx = context;
	struct json_object *parent = ctx->ctrls;
	struct json_object *iter;
	struct json_object *array;
	int i;
	int idx;

	i = find_array(parent, tagAlias, alias, &iter);
	if (i < 0)
		return -ENOENT;

	json_object_object_get_ex(iter, tagSubsystems, &array);
	if (!array)
		return -ENOENT;

	idx = find_string(array, old);
	if (idx < 0)
		return -ENOENT;

	json_object_array_put_idx(array, idx,
				  json_object_new_string(new));

	rename_host_acl(ctx->hosts, old, new);

	return 0;
}

int add_acl(void *context, char *nqn, char *ss)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;

	return add_to_array(array, tagNQN, nqn, tagACL, ss);
}

int del_acl(void *context, char *nqn, char *ss)
{
	struct json_context *ctx = context;
	struct json_object *array = ctx->hosts;

	return del_from_array(array, tagNQN, nqn, tagACL, ss);
}

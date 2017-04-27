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

#include "json.h"
#include "tags.h"
#include "common.h"

/* helper functions */

static char *error(int ret)
{
	if (ret == -ENOENT)
		return "not found";
	if (ret == -EEXIST)
		return "already exists";
	return "unknown error";
}

static int find_array(json_t *array, const char *tag, char *val,
		      json_t **result)
{
	json_t			*iter;
	json_t			*obj;
	int			 i, n;

	n = json_array_size(array);
	for (i = 0; i < n; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;
		obj = json_object_get(iter, tag);
		if (obj && json_is_string(obj) &&
		    strcmp(json_string_value(obj), val) == 0) {
			if (result)
				*result = iter;
			return i;
		}
	}

	if (result)
		*result = NULL;

	return -ENOENT;
}

static int list_array(json_t *array, char *tag, char *response)
{
	json_t			*iter;
	json_t			*obj;
	int			 i, n, cnt, total = 0;

	cnt = json_array_size(array);
	if (!cnt)
		return -ENOENT;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;
		obj = json_object_get(iter, tag);
		if (obj && json_is_string(obj)) {
			n = sprintf(response, "%s\"%s\"", i ? "," : "",
				    json_string_value(obj));
			response += n;
			total += n;
		}
	}

	return total;
}

static int del_from_array(json_t *parent, const char *tag,
			  char *value, const char *subgroup, char *ss)
{
	json_t			*array;
	json_t			*obj;
	int			 i;

	i = find_array(parent, tag, value, &obj);
	if (i < 0)
		goto err;

	array = json_object_get(obj, subgroup);
	if (!array)
		goto err;

	i = find_array(array, TAG_NQN, ss, NULL);
	if (i < 0)
		goto err;

	json_array_remove(array, i);

	return 0;
err:
	return -ENOENT;
}

/* walk all host acls for ss name change */
static void rename_host_acl(json_t *hosts, char *old, char *new)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	json_t			*tmp;
	int			 idx;
	int			 i;
	int			 cnt;

	cnt = json_array_size(hosts);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(hosts, i);

		array = json_object_get(iter, TAG_ACL);
		if (!array)
			continue;

		idx = find_array(array, TAG_NQN, old, &obj);
		if (idx >= 0)
			json_set_string(obj, TAG_NQN, new);
	}
}

/* walk all host acls for ss deletion */
static void del_host_acl(json_t *hosts, char *nqn)
{
	json_t			*array;
	json_t			*obj;
	int			 idx;
	int			 i;
	int			 cnt;

	cnt = json_array_size(hosts);

	for (i = 0; i < cnt; i++) {
		obj = json_array_get(hosts, i);

		array = json_object_get(obj, TAG_ACL);
		if (!array)
			continue;

		idx = find_array(array, TAG_NQN, nqn, NULL);
		if (idx >= 0)
			json_array_remove(array, idx);
	}
}

static void parse_config_file(struct json_context *ctx)
{
	json_t			*root;
	json_t			*ctrls;
	json_t			*hosts;
	json_error_t		 error;
	int			 dirty = 0;

	root = json_load_file(ctx->filename, JSON_DECODE_ANY, &error);
	if (!root) {
		root = json_object();
		ctrls = json_array();
		json_object_set_new(root, TAG_CTRLS, ctrls);
		hosts = json_array();
		json_object_set_new(root, TAG_HOSTS, hosts);
		dirty = 1;
	} else {
		ctrls = json_object_get(root, TAG_CTRLS);
		if (!ctrls) {
			ctrls = json_array();
			json_object_set_new(root, TAG_CTRLS, ctrls);
			dirty = 1;
		}
		hosts = json_object_get(root, TAG_HOSTS);
		if (!hosts) {
			hosts = json_array();
			json_object_set_new(root, TAG_HOSTS, hosts);
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
	struct json_context	*ctx = context;
	json_t			*root = ctx->root;
	char			*filename = ctx->filename;
	int			 ret;

	ret = json_dump_file(root, filename, 0);
	if (ret)
		fprintf(stderr, "json_dump_file failed %d\n", ret);
}

void *init_json(char *filename)
{
	struct json_context	*ctx;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	strncpy(ctx->filename, filename, sizeof(ctx->filename));

	pthread_spin_init(&ctx->lock, PTHREAD_PROCESS_SHARED);

	parse_config_file(ctx);

	return ctx;
}

void cleanup_json(void *context)
{
	struct json_context	*ctx = context;

	json_decref(ctx->root);

	pthread_spin_destroy(&ctx->lock);

	free(ctx);
}

int del_ctrl(void *context, char *alias)
{
	struct json_context	*ctx = context;
	json_t			*parent = ctx->ctrls;
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	json_t			*ss;
	int			 i, n, idx;

	idx = find_array(parent, TAG_ALIAS, alias, &iter);
	if (idx < 0)
		return -ENOENT;

	array = json_object_get(iter, TAG_SUBSYSTEMS);
	if (array) {
		n = json_array_size(array);

		for (i = 0; i < n; i++) {
			obj = json_array_get(array, 0);
			ss = json_object_get(obj, TAG_NQN);
			del_subsys(ctx, alias,
				   (char *) json_string_value(ss));
		}
	}

	json_array_remove(parent, idx);

	return 0;
}

int list_ctrl(void *context, char *response)
{
	struct json_context	*ctx = context;
	json_t			*array = ctx->ctrls;
	int			 n;

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
	struct json_context	*ctx = context;
	json_t			*array = ctx->ctrls;
	json_t			*iter;
	json_t			*tmp;
	int			 i;

	if (find_array(array, TAG_ALIAS, new, NULL) >= 0)
		return -EEXIST;

	i = find_array(array, TAG_ALIAS, old, &iter);
	if (i < 0)
		return -ENOENT;

	json_set_string(iter, TAG_ALIAS, new);

	return 0;
}

static void add_transport(json_t *parent, json_t *new)
{
	json_t			*subgroup;
	json_t			*value;
	json_t			*tmp;

	json_get_subgroup(parent, TAG_TRANSPORT, subgroup);

	json_update_string(subgroup, new, TAG_TYPE, value);
	json_update_string(subgroup, new, TAG_FAMILY, value);
	json_update_string(subgroup, new, TAG_ADDRESS, value);
	json_update_int(subgroup, new, TAG_PORT, value);
}

static void add_subsys(json_t *parent, json_t *newparent)
{
	json_t			*subgroup;
	json_t			*new;
	json_t			*obj;
	json_t			*value;
	json_t			*tmp;
	int			 i, n;

	json_get_array(parent, TAG_SUBSYSTEMS, subgroup);
	if (!subgroup)
		return;

	new = json_object_get(newparent, TAG_SUBSYSTEMS);
	if (!new)
		return;

	n = json_array_size(new);

	for (i = 0; i < n; i++) {
		obj = json_array_get(new, i);

		json_update_string(subgroup, obj, TAG_NQN, value);
		json_update_int(subgroup, new, TAG_ALLOW_ALL, value);
	}
}

int set_ctrl(void *context, char *alias, char *data)
{
	struct json_context	*ctx = context;
	json_t			*array = ctx->ctrls;
	json_t			*iter;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	int			 i;

	i = find_array(array, TAG_ALIAS, alias, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_string(iter, TAG_ALIAS, alias);
		json_array_append_new(array, iter);
	}

	if (strlen(data) == 0)
		return 0;

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return -ENOMEM;

	json_update_string(iter, new, TAG_ALIAS, value);
	json_update_int(iter, new, TAG_REFRESH, value);

	add_transport(iter, new);

	add_subsys(iter, new);

	json_decref(new);

	return 0;
}

static int show_subsys(json_t *parent, char *response)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	char			*p = response;
	int			 i, n, cnt;

	array = json_object_get(parent, TAG_SUBSYSTEMS);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	n = sprintf(p, ",\"%s\":[", TAG_SUBSYSTEMS);
	p += n;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NQN);
		if (!obj)
			continue;

		n = sprintf(p, "%s{\"%s\":\"%s\"", i ? "," : "",
			    TAG_NQN, json_string_value(obj));
		p += n;

		obj = json_object_get(iter, TAG_ALLOW_ALL);
		if (obj) {
			n = sprintf(p, ",\"%s\":%lld", TAG_ALLOW_ALL,
				    json_integer_value(obj));
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

static int show_transport(json_t *parent, char *response)
{
	json_t			*iter;
	json_t			*obj;
	char			*p = response;
	int			 n;

	iter = json_object_get(parent, TAG_TRANSPORT);
	if (!iter)
		goto err;

	n = sprintf(p, ",\"%s\":{", TAG_TRANSPORT);
	p += n;

	obj = json_object_get(iter, TAG_TYPE);
	if (obj) {
		n = sprintf(p, "\"%s\":\"%s\"", TAG_TYPE,
			    json_string_value(obj));
		p += n;
	}

	obj = json_object_get(iter, TAG_FAMILY);
	if (obj) {
		n = sprintf(p, ",\"%s\":\"%s\"", TAG_FAMILY,
			    json_string_value(obj));
		p += n;
	}

	obj = json_object_get(iter, TAG_ADDRESS);
	if (obj) {
		n = sprintf(p, ",\"%s\":\"%s\"", TAG_ADDRESS,
			    json_string_value(obj));
		p += n;
	}

	obj = json_object_get(iter, TAG_PORT);
	if (obj) {
		n = sprintf(p, ",\"%s\":%lld", TAG_PORT,
			    json_integer_value(obj));
		p += n;
	}
	n = sprintf(p, "}");
	p += n;

	return p - response;
err:
	return sprintf(response, ",\"%s\":{}", TAG_TRANSPORT);
}

int show_ctrl(void *context, char *alias, char *response)
{
	struct json_context	*ctx = context;
	json_t			*array = ctx->ctrls;
	json_t			*obj;
	json_t			*ctrl;

	int			 i;

	i = find_array(array, TAG_ALIAS, alias, &ctrl);
	if (i < 0)
		return -ENOENT;

	i = sprintf(response, "{\"%s\":{\"%s\":\"%s\"",
		    TAG_CTRLS, TAG_ALIAS, alias);
	response += i;

	obj = json_object_get(ctrl, TAG_REFRESH);
	if (obj) {
		i = sprintf(response, ",\"%s\":%lld", TAG_REFRESH,
			    json_integer_value(obj));
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
	struct json_context	*ctx = context;
	json_t			*array = ctx->hosts;
	json_t			*iter;
	json_t			*tmp;
	int			 i;

	i = find_array(array, TAG_NQN, nqn, &iter);
	if (i >= 0)
		return -EEXIST;

	iter = json_object();
	json_set_string(iter, TAG_NQN, nqn);
	json_array_append_new(array, iter);

	return 0;
}

int del_host(void *context, char *nqn)
{
	struct json_context	*ctx = context;
	json_t			*array = ctx->hosts;
	json_t			*iter;
	int			 i;

	i = find_array(array, TAG_NQN, nqn, &iter);
	if (i < 0)
		return -ENOENT;

	json_array_remove(array, i);

	return 0;
}

int list_host(void *context, char *response)
{
	struct json_context	*ctx = context;
	json_t			*array = ctx->hosts;
	int			 n;

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
	struct json_context	*ctx = context;
	json_t			*array = ctx->hosts;
	json_t			*iter;
	json_t			*tmp;
	int			 i;

	if (find_array(array, TAG_NQN, new, NULL) >= 0)
		return -EEXIST;

	i = find_array(array, TAG_NQN, old, &iter);
	if (i < 0)
		return -ENOENT;

	json_set_string(iter, TAG_NQN, new);

	return 0;
}

static int show_acl(json_t *parent, char *response)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	char			*p = response;
	int			 i, n, cnt;

	array = json_object_get(parent, TAG_ACL);
	if (!array)
		goto err;

	cnt = json_array_size(array);

	if (cnt == 0)
		goto err;

	n = sprintf(p, ",\"%s\":[", TAG_ACL);
	p += n;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_NQN);
		if (!obj)
			continue;

		n = sprintf(p, "%s{\"%s\":\"%s\"", i ? "," : "",
			    TAG_NQN, json_string_value(obj));
		p += n;

		obj = json_object_get(iter, TAG_ACCESS);
		if (obj) {
			n = sprintf(p, ",\"%s\":%lld", TAG_ACCESS,
				    json_integer_value(obj));
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
	struct json_context	*ctx = context;
	json_t			*array = ctx->hosts;
	json_t			*obj;
	int			 i;

	i = find_array(array, TAG_NQN, nqn, &obj);
	if (i < 0)
		return -ENOENT;

	i = sprintf(response, "{\"%s\":{\"%s\":\"%s\"",
		    TAG_HOSTS, TAG_NQN, nqn);
	response += i;

	i = show_acl(obj, response);
	response += i;

	sprintf(response, "}}");

	return 0;
}

int set_subsys(void *context, char *alias, char *ss, char *data)
{
	struct json_context	*ctx = context;
	json_t			*parent = ctx->ctrls;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	int			 i;

	i = find_array(parent, TAG_ALIAS, alias, &obj);
	if (i < 0)
		return -ENOENT;

	json_get_array(obj, TAG_SUBSYSTEMS, array);
	if (!array)
		return -EINVAL;

	i = find_array(array, TAG_NQN, ss, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_string(iter, TAG_NQN, ss);
		json_array_append_new(array, iter);
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return -ENOMEM;

	json_update_string(iter, new, TAG_NQN, value);
	json_update_int(iter, new, TAG_ALLOW_ALL, value);

	json_decref(new);

	return 0;
}

int del_subsys(void *context, char *alias, char *ss)
{
	struct json_context	*ctx = context;
	json_t			*parent = ctx->ctrls;
	int			 ret;

	ret = del_from_array(parent, TAG_ALIAS, alias, TAG_SUBSYSTEMS, ss);
	if (!ret)
		del_host_acl(ctx->hosts, ss);

	return ret;
}

int rename_subsys(void *context, char *alias, char *old, char *new)
{
	struct json_context	*ctx = context;
	json_t			*parent = ctx->ctrls;
	json_t			*iter;
	json_t			*array;
	json_t			*obj;
	json_t			*tmp;
	int			 i;
	int			 idx;

	i = find_array(parent, TAG_ALIAS, alias, &iter);
	if (i < 0)
		return -ENOENT;

	array = json_object_get(iter, TAG_SUBSYSTEMS);
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
	struct json_context	*ctx = context;
	json_t			*parent = ctx->hosts;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	int			 i;

	i = find_array(parent, TAG_NQN, nqn, &obj);
	if (i < 0)
		return -ENOENT;

	json_get_array(obj, TAG_ACL, array);
	if (!array)
		return -EINVAL;

	i = find_array(array, TAG_NQN, ss, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_string(iter, TAG_NQN, ss);
		json_array_append_new(array, iter);
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return -ENOMEM;

	json_update_string(iter, new, TAG_NQN, value);
	json_update_int(iter, new, TAG_ACCESS, value);

	json_decref(new);

	return 0;
}

int del_acl(void *context, char *nqn, char *ss)
{
	struct json_context	*ctx = context;
	json_t			*array = ctx->hosts;

	return del_from_array(array, TAG_NQN, nqn, TAG_ACL, ss);
}

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

static int find_array_int(json_t *array, const char *tag, int val,
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
		if (obj && json_is_integer(obj) &&
		    json_integer_value(obj) == val) {
			if (result)
				*result = iter;
			return i;
		}
	}

	if (result)
		*result = NULL;

	return -ENOENT;
}

static int list_array(json_t *array, char *tag, char *resp)
{
	json_t			*iter;
	json_t			*obj;
	char			*p = resp;
	int			 i, n, cnt;

	cnt = json_array_size(array);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;

		obj = json_object_get(iter, tag);
		if (obj && json_is_string(obj))
			array_json_string(obj, p, n);
	}

	return p - resp;
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

	i = find_array(array, TAG_SUBNQN, ss, NULL);
	if (i < 0)
		goto err;

	json_array_remove(array, i);

	return 0;
err:
	return -ENOENT;
}

static int del_int_from_array(json_t *parent, const char *tag,
			      char *value, const char *subgroup,
			      char *key, int val)
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

	i = find_array_int(array, key, val, NULL);
	if (i < 0)
		goto err;

	json_array_remove(array, i);

	return 0;
err:
	return -ENOENT;
}

static void parse_config_file(struct json_context *ctx)
{
	json_t			*root;
	json_error_t		 error;
	int			 dirty = 0;

	root = json_load_file(ctx->filename, JSON_DECODE_ANY, &error);
	if (!root) {
		root = json_object();
		dirty = 1;
	}

	ctx->root = root;

	if (dirty)
		store_config_file(ctx);
}

static json_t *get_group_array(struct json_context *ctx, char *group, char *tag)
{
	int			 idx;
	json_t			*groups;
	json_t			*parent;
	json_t			*array;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups)
		return NULL;

	idx = find_array(groups, TAG_NAME, group, &parent);
	if (idx < 0)
		return NULL;

	array = json_object_get(parent, tag);
	if (!array) {
		array = json_array();
		json_object_set_new(parent, tag, array);
	}

	return array;
}

/* walk all host acls for ss name change */
static void rename_host_acl(struct json_context *ctx, char *group, char *old,
			    char *new)
{
	json_t			*hosts;
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	json_t			*tmp;
	int			 idx;
	int			 i;
	int			 cnt;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts)
		return;

	cnt = json_array_size(hosts);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(hosts, i);

		array = json_object_get(iter, TAG_HOSTS);
		if (!array)
			continue;

		idx = find_array(array, TAG_HOSTNQN, old, &obj);
		if (idx >= 0)
			json_set_string(obj, TAG_HOSTNQN, new);
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

		array = json_object_get(obj, TAG_HOSTS);
		if (!array)
			continue;

		idx = find_array(array, TAG_HOSTNQN, nqn, NULL);
		if (idx >= 0)
			json_array_remove(array, idx);
	}
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

static int _list_target(void *context, char *group, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	char			*p = resp;
	int			 n;

	targets = get_group_array(ctx, group, TAG_TARGETS);

	start_json_array(TAG_TARGETS, p, n);

	if (targets) {
		n = list_array(targets, TAG_ALIAS, p);
		p += n;
	}

	end_json_array(p, n);

	return p - resp;
}

static int _list_host(void *context, char *group, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	char			*p = resp;
	int			 n;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts)
		return 0;

	start_json_array(TAG_HOSTS, p, n);

	if (hosts) {
		n = list_array(hosts, TAG_HOSTNQN, p);
		p += n;
	}

	end_json_array(p, n);

	return p - resp;
}

/* GROUPS */

int add_to_groups(void *context, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*iter = NULL;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;
	int			 len = strlen(data);

	if (!len)
		return -EINVAL;

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_NAME);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined", TAG_NAME);
		ret = -EINVAL;
		goto out;
	}

	strcpy(newname, json_string_value(value));

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		groups = json_array();
		json_object_set_new(ctx->root, TAG_GROUPS, groups);
	}

	i = find_array(groups, TAG_NAME, newname, &iter);
	if (i >= 0) {
		sprintf(resp, "%s '%s' exists", TAG_GROUP, newname);
		ret = -EEXIST;
		goto out;
	}

	iter = json_object();
	json_set_string(iter, TAG_NAME, newname);
	json_array_append_new(groups, iter);

	tmp = json_array();
	json_object_set_new(iter, TAG_TARGETS, tmp);

	tmp = json_array();
	json_object_set_new(iter, TAG_HOSTS, tmp);

	sprintf(resp, "%s '%s' added", TAG_GROUP, newname);
out:
	json_decref(new);

	return ret;
}

int add_a_group(void *context, char *group, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*iter = NULL;
	json_t			*tmp;
	int			 i;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		groups = json_array();
		json_object_set_new(ctx->root, TAG_GROUPS, groups);
	} else {
		i = find_array(groups, TAG_NAME, group, NULL);
		if (i >= 0) {
			sprintf(resp, "%s '%s' exists", TAG_GROUP, group);
			return -EEXIST;
		}
	}

	iter = json_object();
	json_set_string(iter, TAG_NAME, group);
	json_array_append_new(groups, iter);

	tmp = json_array();
	json_object_set_new(iter, TAG_TARGETS, tmp);

	tmp = json_array();
	json_object_set_new(iter, TAG_HOSTS, tmp);

	sprintf(resp, "%s '%s' added", TAG_GROUP, group);

	return 0;
}

int update_group(void *context, char *group, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*iter = NULL;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;
	int			 len = strlen(data);

	if (len == 0)
		return -EINVAL;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		groups = json_array();
		json_object_set_new(ctx->root, TAG_GROUPS, groups);
	}

	i = find_array(groups, TAG_NAME, group, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_NAME);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined", TAG_NAME);
		ret = -EINVAL;
		goto out;
	}

	strcpy(newname, json_string_value(value));

	i = find_array(groups, TAG_NAME, newname, NULL);
	if (i >= 0) {
		sprintf(resp, "%s '%s' exists", TAG_GROUP, newname);
		ret = -EEXIST;
		goto out;
	}

	json_set_string(iter, TAG_NAME, newname);

	sprintf(resp, "%s '%s' renamed to '%s'", TAG_GROUP, group, newname);
out:
	json_decref(new);

	return ret;
}

int del_group(void *context, char *group, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	int			 i;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(groups, TAG_NAME, group, NULL);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	json_array_remove(groups, i);

	sprintf(resp, "%s '%s' deleted", TAG_GROUP, group);

	return 0;
}

int list_group(void *context, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	char			*p = resp;
	int			 n;

	groups = json_object_get(ctx->root, TAG_GROUPS);

	n = sprintf(p, "{" JSARRAY, TAG_GROUPS);
	p += n;

	if (groups) {
		n = list_array(groups, TAG_NAME, p);
		p += n;
	}

	sprintf(p, "]}");

	return 0;
}

int show_group(void *context, char *group, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*obj;
	char			*p = resp;
	int			 i, n;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(groups, TAG_NAME, group, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	n = sprintf(p, "{" JSSTR ",", TAG_NAME, group);
	p += n;

	n = _list_target(context, group, p);
	p += n;

	n = sprintf(p, ",");
	p += n;

	n = _list_host(context, group, p);
	p += n;

	sprintf(p, "}");

	return 0;
}

/* HOST INTERFACES */

int set_interface(void *context, char *group, char *host, char *data,
		  char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	json_t			*obj;
	json_t			*new;
	json_t			*tmp;
	json_t			*array;
	json_t			*value;
	json_error_t		 error;
	char			 trtype[16];
	char			 adrfam[16];
	char			 traddr[128];
	int			 ret;
	int			 i, cnt = 0;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_HOSTNQN, host, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOST, host, TAG_GROUP, group);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_TYPE);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_TYPE);
		ret = -EINVAL;
		goto out;
	}

	sprintf(trtype, "%.*s", (int) sizeof(trtype) - 1,
		(char *) json_string_value(value));

	value = json_object_get(new, TAG_ADDRESS);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_ADDRESS);
		ret = -EINVAL;
		goto out;
	}

	sprintf(traddr, "%.*s", (int) sizeof(traddr) - 1,
		(char *) json_string_value(value));

	value = json_object_get(new, TAG_FAMILY);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_FAMILY);
		ret = -EINVAL;
		goto out;
	}

	sprintf(adrfam, "%.*s", (int) sizeof(adrfam) - 1,
		(char *) json_string_value(value));

	array = json_object_get(iter, TAG_INTERFACES);
	if (!array) {
		array = json_array();
		json_object_set_new(iter, TAG_INTERFACES, array);
	} else
		cnt = json_array_size(array);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		value = json_object_get(iter, TAG_TYPE);
		if (strcmp(trtype, (char *) json_string_value(value)) != 0)
			continue;

		value = json_object_get(iter, TAG_ADDRESS);
		if (strcmp(traddr, (char *) json_string_value(value)) != 0)
			continue;

		value = json_object_get(iter, TAG_FAMILY);
		if (strcmp(adrfam, (char *) json_string_value(value)) != 0)
			continue;

		sprintf(resp, "%s exists for %s '%s' in %s '%s'",
			TAG_INTERFACE, TAG_HOST, host, TAG_GROUP, group);

		ret = -EEXIST;
		goto out;
	}

	obj = json_object();

	json_set_string(obj, TAG_TYPE, trtype);
	json_set_string(obj, TAG_FAMILY, adrfam);
	json_set_string(obj, TAG_ADDRESS, traddr);

	json_array_append_new(array, obj);

	sprintf(resp, "%s added to %s '%s' in %s '%s'",
		TAG_INTERFACE, TAG_HOST, host, TAG_GROUP, group);
	ret = 0;
out:
	json_decref(new);

	return ret;
}

int del_interface(void *context, char *group, char *host, char *data,
		  char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	json_t			*new;
	json_t			*array;
	json_t			*value;
	json_error_t		 error;
	char			 trtype[16];
	char			 adrfam[16];
	char			 traddr[128];
	int			 ret;
	int			 i, cnt = 0;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_HOSTNQN, host, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOST, host, TAG_GROUP, group);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_TYPE);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_TYPE);
		ret = -EINVAL;
		goto out;
	}

	sprintf(trtype, "%.*s", (int) sizeof(trtype) - 1,
		(char *) json_string_value(value));

	value = json_object_get(new, TAG_ADDRESS);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_ADDRESS);
		ret = -EINVAL;
		goto out;
	}

	sprintf(traddr, "%.*s", (int) sizeof(traddr) - 1,
		(char *) json_string_value(value));

	value = json_object_get(new, TAG_FAMILY);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_FAMILY);
		ret = -EINVAL;
		goto out;
	}

	sprintf(adrfam, "%.*s", (int) sizeof(adrfam) - 1,
		(char *) json_string_value(value));

	array = json_object_get(iter, TAG_INTERFACES);
	if (!array)
		goto notfound;

	cnt = json_array_size(array);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		value = json_object_get(iter, TAG_TYPE);
		if (strcmp(trtype, (char *) json_string_value(value)) != 0)
			continue;

		value = json_object_get(iter, TAG_ADDRESS);
		if (strcmp(traddr, (char *) json_string_value(value)) != 0)
			continue;

		value = json_object_get(iter, TAG_FAMILY);
		if (strcmp(adrfam, (char *) json_string_value(value)) != 0)
			continue;

		json_array_remove(array, i);

		sprintf(resp, "%s deleted from %s '%s' in %s '%s'",
			TAG_INTERFACE, TAG_HOST, host, TAG_GROUP, group);
		ret = 0;
		goto out;
	}
notfound:
	sprintf(resp, "%s does not exist for %s '%s' in %s '%s'",
		TAG_INTERFACE, TAG_HOST, host, TAG_GROUP, group);
	ret = -ENOENT;

out:
	json_decref(new);

	return ret;
}
/* HOSTS */

int add_to_hosts(void *context, char *group, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;
	int			 added = 0;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_HOSTNQN);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_HOSTNQN);
		ret = -EINVAL;
		goto out;
	}

	strcpy(newname, (char *) json_string_value(value));

	i = find_array(hosts, TAG_HOSTNQN, newname, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_string(iter, TAG_HOSTNQN, newname);
		json_array_append_new(hosts, iter);
		added = 1;
	}

	sprintf(resp, "%s '%s' %s %s '%s'", TAG_HOST, newname,
		added ? "added to" : "updated in ", TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

int add_a_host(void *context, char *group, char *nqn, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	json_t			*tmp;
	int			 i;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_HOSTNQN, nqn, &iter);
	if (i >= 0) {
		sprintf(resp, "%s '%s' exists in %s '%s'",
			TAG_HOST, nqn, TAG_GROUP, group);
		return -EEXIST;
	}

	iter = json_object();
	json_set_string(iter, TAG_HOSTNQN, nqn);
	json_array_append_new(hosts, iter);

	sprintf(resp, "%s '%s' added to %s '%s'",
		TAG_HOST, nqn, TAG_GROUP, group);

	return 0;
}

int update_host(void *context, char *group, char *nqn, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_HOSTNQN);
	if (value) {
		strcpy(newname, (char *) json_string_value(value));

		i = find_array(hosts, TAG_HOSTNQN, newname, &iter);
		if (i >= 0) {
			sprintf(resp, "%s '%s' exists in %s '%s'",
				TAG_HOST, newname, TAG_GROUP, group);
			ret = -EEXIST;
			goto out;
		}
	}

	i = find_array(hosts, TAG_HOSTNQN, nqn, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOST, nqn, TAG_GROUP, group);
		ret = -ENOENT;
		goto out;
	}

	json_update_string(iter, new, TAG_HOSTNQN, value);

	sprintf(resp, "%s '%s' updated in %s '%s'",
		TAG_HOST, nqn, TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

int del_host(void *context, char *group, char *nqn, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	int			 i;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_HOSTNQN, nqn, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOST, nqn, TAG_GROUP, group);
		return -ENOENT;
	}

	json_array_remove(hosts, i);

	sprintf(resp, "%s '%s' deleted from %s '%s'",
		TAG_HOST, nqn, TAG_GROUP, group);

	return 0;
}

int list_host(void *context, char *group, char *resp)
{
	char			*p = resp;
	int			 n;

	n = sprintf(p, "{");
	p += n;

	n = _list_host(context, group, p);
	p += n;

	sprintf(p, "}");

	return 0;
}

int show_host(void *context, char *group, char *nqn, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*obj;
	int			 i;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_HOSTNQN, nqn, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOST, nqn, TAG_GROUP, group);
		return -ENOENT;
	}

	return sprintf(resp, "%s", json_dumps(obj, 0));
}

/* SUBSYSTEMS */

int set_subsys(void *context, char *group, char *alias, char *ss, char *data,
	       int create, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	int			 i;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(obj, TAG_SUBSYSTEMS, array);
	if (!array)
		return -EINVAL;

	i = find_array(array, TAG_SUBNQN, ss, &iter);
	if (i < 0) {
		if (!create) {
			sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
				TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
				TAG_GROUP, group);
			return -ENOENT;
		}
		iter = json_object();
		json_set_string(iter, TAG_SUBNQN, ss);
		json_array_append_new(array, iter);
	}

	if (strlen(data) == 0) {
		if (create)
			return 0;
		sprintf(resp, "no data to update %s '%s' in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias, TAG_GROUP, group);
		return -EINVAL;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	if (!create) {
		json_update_string(iter, new, TAG_SUBNQN, value);
		rename_host_acl(ctx, group, ss,
				(char *) json_string_value(value));
	}

	json_update_int(iter, new, TAG_ALLOW_ALL, value);

	json_decref(new);

	sprintf(resp, "%s '%s' updated in %s '%s' in %s '%s'",
		TAG_SUBSYSTEM, ss, TAG_TARGET, alias, TAG_GROUP, group);

	return 0;
}

int del_subsys(void *context, char *group, char *alias, char *ss, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*hosts;
	int			 ret;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (hosts)
		del_host_acl(hosts, ss);

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	ret = del_from_array(targets, TAG_ALIAS, alias, TAG_SUBSYSTEMS, ss);
	if (ret) {
		sprintf(resp,
			"Unable to delete %s '%s' from %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias, TAG_GROUP, group);
		return ret;
	}

	sprintf(resp, "%s '%s' deleted from %s '%s' in %s '%s'",
		TAG_SUBSYSTEM, ss, TAG_TARGET, alias, TAG_GROUP, group);

	return 0;
}

/* DRIVE */

int set_drive(void *context, char *group, char *alias, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*tmp;
	json_error_t		 error;
	char			 val[128];
	int			 i, cnt;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_ALIAS, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(obj, TAG_NSDEVS, array);
	if (!array) {
		array = json_array();
		json_object_set_new(array, TAG_NSDEVS, obj);
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	obj = json_object_get(new, TAG_NSDEV);
	if (!obj) {
		sprintf(resp, "invalid json syntax");
		json_decref(new);
		return -EINVAL;
	}

	strcpy(val, json_string_value(obj));

	json_decref(new);

	cnt = json_array_size(array);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;
		obj = json_object_get(iter, TAG_NSDEV);
		if (!obj || !json_is_string(obj))
			continue;
		if (strcmp(json_string_value(obj), val) == 0) {
			sprintf(resp,
				"%s '%s' exists in %s '%s' in %s '%s'",
				TAG_NSDEV, val, TAG_TARGET, alias,
				TAG_GROUP, group);
			return -EEXIST;
		}
	}

	obj = json_object();
	json_set_string(obj, TAG_NSDEV, val);
	json_array_append_new(array, obj);

	sprintf(resp, "Added %s '%s' to %s '%s' in %s '%s'",
		TAG_NSDEV, val, TAG_TARGET, alias, TAG_GROUP, group);

	return 0;
}

int del_drive(void *context, char *group, char *alias, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*array;
	json_t			*new;
	json_t			*iter;
	json_t			*obj;
	json_error_t		 error;
	char			 val[128];
	int			 i;
	int			 cnt;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_ALIAS, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(obj, TAG_NSDEVS, array);
	if (!array) {
		array = json_array();
		json_object_set_new(array, TAG_NSDEVS, obj);
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	obj = json_object_get(new, TAG_NSDEV);
	if (!obj) {
		sprintf(resp, "invalid json syntax");
		json_decref(new);
		return -EINVAL;
	}

	strcpy(val, json_string_value(obj));

	json_decref(new);

	cnt = json_array_size(array);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;
		obj = json_object_get(iter, TAG_NSDEV);
		if (!obj || !json_is_string(obj))
			continue;
		if (strcmp(json_string_value(obj), val) == 0)
			break;
	}

	if (i == cnt) {
		sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
			TAG_NSDEV, val, TAG_TARGET, alias,
			TAG_GROUP, group);
		return -ENOENT;
	}

	json_array_remove(array, i);

	sprintf(resp, "%s '%s' deleted from %s '%s' in %s '%s'",
		TAG_NSDEV, val, TAG_TARGET, alias, TAG_GROUP, group);
	return 0;
}

/* PORTID */

int set_portid(void *context, char *group, char *alias, int portid,
	       char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	int			 i, ret = 0;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(obj, TAG_PORTIDS, array);
	if (!array)
		return -EINVAL;

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (strlen(data) == 0) {
		sprintf(resp, "no data to update %s '%d' in %s '%s' in %s '%s'",
			TAG_PORTID, portid, TAG_TARGET, alias,
			TAG_GROUP, group);

		return -EINVAL;
	}

	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	if (!portid) {
		tmp = json_object_get(new, TAG_PORTID);
		portid = json_integer_value(tmp);
		if (!portid) {
			sprintf(resp, "no port id give for %s '%s' in %s '%s'",
				TAG_TARGET, alias, TAG_GROUP, group);
			ret = -EINVAL;
			goto out;
		}
	}

	i = find_array_int(array, TAG_PORTID, portid, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_int(iter, TAG_PORTID, portid);
		json_array_append_new(array, iter);
	}

	json_update_string(iter, new, TAG_TYPE, value);
	json_update_string(iter, new, TAG_ADDRESS, value);
	json_update_string(iter, new, TAG_FAMILY, value);
	json_update_string(iter, new, TAG_TREQ, value);
	json_update_int(iter, new, TAG_TRSVCID, value);

	sprintf(resp, "%s '%d' updated in %s '%s' in %s '%s'",
		TAG_PORTID, portid, TAG_TARGET, alias, TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

int del_portid(void *context, char *group, char *alias, int portid,
	       char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	int			 ret;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	ret = del_int_from_array(targets, TAG_ALIAS, alias, TAG_PORTIDS,
				 TAG_PORTID, portid);
	if (ret) {
		sprintf(resp,
			"Unable to delete %s '%d' from %s '%s' in %s '%s'",
			TAG_PORTID, portid, TAG_TARGET, alias,
			TAG_GROUP, group);
		return ret;
	}

	sprintf(resp, "%s '%d' deleted from %s '%s' in %s '%s'",
		TAG_PORTID, portid, TAG_TARGET, alias, TAG_GROUP, group);

	return 0;
}

/* NAMESPACE */

int set_ns(void *context, char *group, char *alias, char *ss, char *data,
	   char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*subgroup;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	int			 val;
	int			 i;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(subgroup, TAG_SUBSYSTEMS, array);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(array, TAG_SUBNQN, ss, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
			TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(obj, TAG_NSIDS, array);
	if (!array)
		return -EINVAL;

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	obj = json_object_get(new, TAG_NSID);
	if (!obj) {
		sprintf(resp, "invalid json syntax");
		json_decref(new);
		return -EINVAL;
	}

	val = json_integer_value(obj);

	i = find_array_int(array, TAG_NSID, val, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_int(iter, TAG_NSID, val);
		json_array_append_new(array, iter);
	}

	json_update_string(iter, new, TAG_NSDEV, value);

	json_decref(new);

	sprintf(resp, "%s %d updated in %s '%s' of %s '%s' in %s '%s'",
		TAG_NSID, val, TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
		TAG_GROUP, group);

	return 0;
}

int del_ns(void *context, char *group, char *alias, char *ss, int ns,
	   char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*subgroup;
	json_t			*array;
	int			 i;
	int			 ret;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	array = json_object_get(subgroup, TAG_SUBSYSTEMS);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
			TAG_GROUP, group);
		return -ENOENT;
	}

	ret = del_int_from_array(array, TAG_SUBNQN, ss, TAG_NSIDS,
				 TAG_NSID, ns);
	if (ret) {
		sprintf(resp,
			"Unable to delete %s '%d' from %s '%s' "
			"in %s '%s' in %s '%s'",
			TAG_NSID, ns, TAG_SUBSYSTEM, ss, TAG_TARGET,
			alias, TAG_GROUP, group);
		return ret;
	}

	sprintf(resp, "%s '%d' deleted from %s '%s' in %s '%s' in %s '%s'",
		TAG_NSID, ns, TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
		TAG_GROUP, group);

	return 0;
}

/* TARGET */

int del_target(void *context, char *group, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	json_t			*ss;
	int			 i, n, idx;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	idx = find_array(targets, TAG_ALIAS, alias, &iter);
	if (idx < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	array = json_object_get(iter, TAG_SUBSYSTEMS);
	if (array) {
		n = json_array_size(array);

		for (i = 0; i < n; i++) {
			obj = json_array_get(array, 0);
			ss = json_object_get(obj, TAG_SUBNQN);
			del_subsys(ctx, group, alias,
				   (char *) json_string_value(ss), resp);
		}
	}

	json_array_remove(targets, idx);

	sprintf(resp, "%s '%s' deleted from %s '%s'",
		TAG_TARGET, alias, TAG_GROUP, group);

	return 0;
}

int list_target(void *context, char *group, char *resp)
{
	char			*p = resp;
	int			 n;

	n = sprintf(p, "{");
	p += n;

	n = _list_target(context, group, p);
	p += n;

	sprintf(p, "}");

	return 0;
}

static void add_portid(json_t *parent, json_t *newparent)
{
	json_t			*subgroup;
	json_t			*new;
	json_t			*iter;
	json_t			*value;
	json_t			*tmp;
	int			 i, cnt;

	json_get_array(parent, TAG_PORTIDS, subgroup);
	if (!subgroup)
		return;

	new = json_object_get(newparent, TAG_PORTIDS);
	if (!new)
		return;

	cnt = json_array_size(new);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(new, i);

		json_update_int(iter, new, TAG_PORTID, value);
		json_update_string(iter, new, TAG_TYPE, value);
		json_update_string(iter, new, TAG_FAMILY, value);
		json_update_string(iter, new, TAG_ADDRESS, value);
		json_update_int(iter, new, TAG_TRSVCID, value);

		json_array_append_new(subgroup, iter);
	}
}

static void add_nsdevice(json_t *parent, json_t *newparent)
{
	json_t			*subgroup;
	json_t			*new;
	json_t			*iter;
	json_t			*tmp;
	int			 i, cnt;
	int			 j, n;

	json_get_array(parent, TAG_NSDEVS, subgroup);
	if (!subgroup)
		return;

	new = json_object_get(newparent, TAG_NSDEVS);
	if (!new)
		return;

	cnt = json_array_size(new);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(new, i);
		if (!json_is_string(iter))
			continue;

		n = json_array_size(subgroup);
		for (j = 0; j < n; j++) {
			tmp = json_array_get(subgroup, j);
			if (!json_is_string(tmp))
				continue;
			if (strcmp(json_string_value(iter),
				   json_string_value(tmp)) == 0)
				break;
		}
		if (n == j)
			json_array_append_new(subgroup, iter);
	}
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

		json_update_string(subgroup, obj, TAG_SUBNQN, value);
		json_update_int(subgroup, new, TAG_ALLOW_ALL, value);
	}
}

int add_to_targets(void *context, char *group, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*iter;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;
	int			 added = 0;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_ALIAS);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined", TAG_ALIAS);
		ret = -EINVAL;
		goto out;
	}

	strcpy(newname, (char *) json_string_value(value));

	i = find_array(targets, TAG_ALIAS, newname, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_string(iter, TAG_ALIAS, newname);
		json_array_append_new(targets, iter);
		added = 1;
	}

	json_update_int(iter, new, TAG_REFRESH, value);

	add_portid(iter, new);

	add_subsys(iter, new);

	add_nsdevice(iter, new);

	sprintf(resp, "%s '%s' %s %s '%s'", TAG_TARGET, newname,
		added ? "added to" : "updated in ", TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

int add_a_target(void *context, char *group, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*iter;
	json_t			*tmp;
	int			 i;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &iter);
	if (i >= 0) {
		sprintf(resp, "%s '%s' exists in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -EEXIST;
	}

	iter = json_object();
	json_set_string(iter, TAG_ALIAS, alias);
	json_array_append_new(targets, iter);

	sprintf(resp, "%s '%s' added to %s '%s'",
		TAG_TARGET, alias, TAG_GROUP, group);

	return 0;
}

int update_target(void *context, char *group, char *alias, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*iter;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_ALIAS);
	if (value) {
		strcpy(newname, (char *) json_string_value(value));
		if (strcmp(alias, newname)) {
			i = find_array(targets, TAG_ALIAS, newname, NULL);
			if (i >= 0) {
				sprintf(resp, "%s '%s' exists in %s '%s'",
					TAG_TARGET, newname, TAG_GROUP, group);
				ret = -EEXIST;
				goto out;
			}
		}
		json_set_string(iter, TAG_ALIAS, newname);
		alias = newname;
	}

	json_update_int(iter, new, TAG_REFRESH, value);

	add_portid(iter, new);

	add_subsys(iter, new);

	add_nsdevice(iter, new);

	sprintf(resp, "%s '%s' updated in %s '%s'",
		TAG_TARGET, alias, TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

int show_target(void *context, char *group, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*obj;
	int			 i;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	return sprintf(resp, "%s", json_dumps(obj, 0));
}

int set_acl(void *context, char *group, char *alias, char *ss, char *nqn,
	    char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*hosts;
	json_t			*subgroup;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*tmp;
	int			 i;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(subgroup, TAG_SUBSYSTEMS, array);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
			TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(array, TAG_SUBNQN, ss, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
			TAG_GROUP, group);
		return -ENOENT;
	}

	array = json_object_get(obj, TAG_HOSTS);
	if (!array) {
		array = json_array();
		json_object_set_new(obj, TAG_HOSTS, array);
	}

	i = find_array(hosts, TAG_HOSTNQN, nqn, NULL);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOSTNQN, nqn, TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(array, TAG_HOSTNQN, nqn, &iter);
	if (i >= 0) {
		sprintf(resp,
		"%s '%s' exists for %s '%s' in %s '%s' in %s '%s'",
		TAG_HOST, nqn, TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
		TAG_GROUP, group);
		return -EEXIST;
	}

	iter = json_object();
	json_set_string(iter, TAG_HOSTNQN, nqn);
	json_array_append_new(array, iter);

	sprintf(resp,
		"%s '%s' added for %s '%s' in %s '%s' in %s '%s'",
		TAG_HOST, nqn, TAG_SUBSYSTEM, ss, TAG_TARGET, alias,
		TAG_GROUP, group);

	return 0;
}

int del_acl(void *context, char *group, char *alias, char *ss, char *nqn,
	    char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*subgroup;
	json_t			*array;
	int			 ret;
	int			 i;

	targets = get_group_array(ctx, group, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_TARGET, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(subgroup, TAG_SUBSYSTEMS, array);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_ALIAS, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	ret = del_from_array(array, TAG_SUBSYSTEM, ss, TAG_HOSTS, nqn);
	if (ret) {
		sprintf(resp,
			"%s '%s' not found in %s '%s' for %s '%s' in %s '%s'",
			TAG_HOST, nqn, TAG_SUBSYSTEM, ss, TAG_ALIAS, alias,
			TAG_GROUP, group);
		return ret;
	}

	sprintf(resp,
		"%s '%s' deleted from %s '%s' for %s '%s' in %s '%s'",
		TAG_HOST, nqn, TAG_SUBSYSTEM, ss, TAG_ALIAS, alias,
		TAG_GROUP, group);

	return 0;
}

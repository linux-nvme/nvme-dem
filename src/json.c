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

static int list_array(json_t *array, char *tag, char *resp)
{
	json_t			*iter;
	json_t			*obj;
	int			 i, n, cnt, total = 0;

	cnt = json_array_size(array);
	if (!cnt)
		return 0;

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;
		obj = json_object_get(iter, tag);
		if (obj && json_is_string(obj)) {
			n = sprintf(resp, "%s\"%s\"", i ? "," : "",
				    json_string_value(obj));
			resp += n;
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

int del_ctlr(void *context, char *group, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*ctlrs;
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	json_t			*ss;
	int			 i, n, idx;

	ctlrs = get_group_array(ctx, group, TAG_CTLRS);
	if (!ctlrs) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	idx = find_array(ctlrs, TAG_ALIAS, alias, &iter);
	if (idx < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_CTLR, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	array = json_object_get(iter, TAG_SUBSYSTEMS);
	if (array) {
		n = json_array_size(array);

		for (i = 0; i < n; i++) {
			obj = json_array_get(array, 0);
			ss = json_object_get(obj, TAG_NQN);
			del_subsys(ctx, group, alias,
				   (char *) json_string_value(ss), resp);
		}
	}

	json_array_remove(ctlrs, idx);

	sprintf(resp, "%s '%s' deleted from %s '%s'",
		TAG_CTLR, alias, TAG_GROUP, group);

	return 0;
}

static int _list_ctlr(void *context, char *group, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*ctlrs;
	int			 n;
	int			 cnt = 0;

	ctlrs = get_group_array(ctx, group, TAG_CTLRS);

	n = sprintf(resp, "\"%s\":[", TAG_CTLRS);
	resp += n;
	cnt += n;

	if (ctlrs) {
		n = list_array(ctlrs, TAG_ALIAS, resp);
		resp += n;
		cnt += n;
	}

	n = sprintf(resp, "]");
	cnt += n;

	return cnt;
}

int list_ctlr(void *context, char *group, char *resp)
{
	int			 n;

	n = sprintf(resp, "{");
	resp += n;

	n = _list_ctlr(context, group, resp);
	resp += n;

	sprintf(resp, "}");

	return 0;
}

static void add_transport(json_t *parent, json_t *newparent)
{
	json_t			*subgroup;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;

	json_get_subgroup(parent, TAG_TRANSPORT, subgroup);
	json_get_subgroup(newparent, TAG_TRANSPORT, new);

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

int add_to_ctlrs(void *context, char *group, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*ctlrs;
	json_t			*iter;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;
	int			 added = 0;

	ctlrs = get_group_array(ctx, group, TAG_CTLRS);
	if (!ctlrs) {
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

	i = find_array(ctlrs, TAG_ALIAS, newname, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_string(iter, TAG_ALIAS, newname);
		json_array_append_new(ctlrs, iter);
		added = 1;
	}

	json_update_int(iter, new, TAG_REFRESH, value);

	add_transport(iter, new);

	add_subsys(iter, new);

	sprintf(resp, "%s '%s' %s %s '%s'", TAG_CTLR, newname,
		added ? "added to" : "updated in ", TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

int add_a_ctlr(void *context, char *group, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*ctlrs;
	json_t			*iter;
	json_t			*tmp;
	int			 i;

	ctlrs = get_group_array(ctx, group, TAG_CTLRS);
	if (!ctlrs) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(ctlrs, TAG_ALIAS, alias, &iter);
	if (i >= 0) {
		sprintf(resp, "%s '%s' exists in %s '%s'",
			TAG_CTLR, alias, TAG_GROUP, group);
		return -EEXIST;
	}

	iter = json_object();
	json_set_string(iter, TAG_ALIAS, alias);
	json_array_append_new(ctlrs, iter);

	sprintf(resp, "%s '%s' added to %s '%s'",
		TAG_CTLR, alias, TAG_GROUP, group);

	return 0;
}

int update_ctlr(void *context, char *group, char *alias, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*ctlrs;
	json_t			*iter;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;

	ctlrs = get_group_array(ctx, group, TAG_CTLRS);
	if (!ctlrs) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(ctlrs, TAG_ALIAS, alias, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_CTLR, alias, TAG_GROUP, group);
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
			i = find_array(ctlrs, TAG_ALIAS, newname, NULL);
			if (i >= 0) {
				sprintf(resp, "%s '%s' exists in %s '%s'",
					TAG_CTLR, newname, TAG_GROUP, group);
				ret = -EEXIST;
				goto out;
			}
		}
		json_set_string(iter, TAG_ALIAS, newname);
		alias = newname;
	}

	json_update_int(iter, new, TAG_REFRESH, value);

	add_transport(iter, new);

	add_subsys(iter, new);

	sprintf(resp, "%s '%s' updated in %s '%s'",
		TAG_CTLR, alias, TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

static int show_subsys(json_t *parent, char *resp)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	char			*p = resp;
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

	return p - resp;
err:
	return sprintf(resp, ",\"%s\":[]", TAG_SUBSYSTEMS);
}

static int show_transport(json_t *parent, char *resp)
{
	json_t			*iter;
	json_t			*obj;
	char			*p = resp;
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

	return p - resp;
err:
	return sprintf(resp, ",\"%s\":{}", TAG_TRANSPORT);
}

int show_ctlr(void *context, char *group, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*ctlrs;
	json_t			*obj;
	json_t			*ctlr;
	int			 i;

	ctlrs = get_group_array(ctx, group, TAG_CTLRS);
	if (!ctlrs) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(ctlrs, TAG_ALIAS, alias, &ctlr);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_CTLR, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	i = sprintf(resp, "{\"%s\":\"%s\"", TAG_ALIAS, alias);
	resp += i;

	obj = json_object_get(ctlr, TAG_REFRESH);
	if (obj) {
		i = sprintf(resp, ",\"%s\":%lld", TAG_REFRESH,
			    json_integer_value(obj));
		resp += i;
	}

	i = show_transport(ctlr, resp);
	resp += i;

	i = show_subsys(ctlr, resp);
	resp += i;

	sprintf(resp, "}");

	return 0;
}

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

	value = json_object_get(new, TAG_NQN);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined", TAG_NQN);
		ret = -EINVAL;
		goto out;
	}

	strcpy(newname, (char *) json_string_value(value));

	i = find_array(hosts, TAG_NQN, newname, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_string(iter, TAG_NQN, newname);
		json_array_append_new(hosts, iter);
		added = 1;
	}

	json_update_string(iter, new, TAG_CERT, value);
	json_update_string(iter, new, TAG_ALIAS_NQN, value);

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

	i = find_array(hosts, TAG_NQN, nqn, &iter);
	if (i >= 0) {
		sprintf(resp, "%s '%s' exists in %s '%s'",
			TAG_HOST, nqn, TAG_GROUP, group);
		return -EEXIST;
	}

	iter = json_object();
	json_set_string(iter, TAG_NQN, nqn);
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

	value = json_object_get(new, TAG_NQN);
	if (value) {
		strcpy(newname, (char *) json_string_value(value));

		i = find_array(hosts, TAG_NQN, newname, &iter);
		if (i >= 0) {
			sprintf(resp, "%s '%s' exists in %s '%s'",
				TAG_HOST, newname, TAG_GROUP, group);
			ret = -EEXIST;
			goto out;
		}
	}

	i = find_array(hosts, TAG_NQN, nqn, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOST, nqn, TAG_GROUP, group);
		ret = -ENOENT;
		goto out;
	}

	json_update_string(iter, new, TAG_NQN, value);

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

	i = find_array(hosts, TAG_NQN, nqn, &iter);
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

static int _list_host(void *context, char *group, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	int			 n;
	int			 cnt = 0;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts)
		return 0;

	n = sprintf(resp, "\"%s\":[", TAG_HOSTS);
	resp += n;
	cnt += n;

	if (hosts) {
		n = list_array(hosts, TAG_NQN, resp);
		resp += n;
		cnt += n;
	}

	n = sprintf(resp, "]");
	cnt += n;

	return cnt;
}

int list_host(void *context, char *group, char *resp)
{
	int			 n;

	n = sprintf(resp, "{");
	resp += n;

	n = _list_host(context, group, resp);
	resp += n;

	sprintf(resp, "}");

	return 0;
}

static int show_acl(json_t *parent, char *resp)
{
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	char			*p = resp;
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

	return p - resp;
err:
	return sprintf(resp, ",\"%s\":[]", TAG_ACL);
}

int show_host(void *context, char *group, char *nqn, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*obj;
	json_t			*value;
	int			 i;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_NQN, nqn, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOST, nqn, TAG_GROUP, group);
		return -ENOENT;
	}

	i = sprintf(resp, "{\"%s\":\"%s\"", TAG_NQN, nqn);
	resp += i;

	value = json_object_get(obj, TAG_CERT);
	if (value) {
		i = sprintf(resp, ",\"%s\":\"%s\"",
			    TAG_CERT, (char *) json_string_value(value));
		resp += i;
	}

	value = json_object_get(obj, TAG_ALIAS_NQN);
	if (value) {
		i = sprintf(resp, ",\"%s\":\"%s\"",
			    TAG_ALIAS_NQN, (char *) json_string_value(value));
		resp += i;
	}

	i = show_acl(obj, resp);
	resp += i;

	sprintf(resp, "}");

	return 0;
}

int set_subsys(void *context, char *group, char *alias, char *ss, char *data,
	       int create, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*ctlrs;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	int			 i;

	ctlrs = get_group_array(ctx, group, TAG_CTLRS);
	if (!ctlrs) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(ctlrs, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_CTLR, alias, TAG_GROUP, group);
		return -ENOENT;
	}

	json_get_array(obj, TAG_SUBSYSTEMS, array);
	if (!array)
		return -EINVAL;

	i = find_array(array, TAG_NQN, ss, &iter);
	if (i < 0) {
		if (!create) {
			sprintf(resp, "%s '%s' not found in %s '%s' in %s '%s'",
				TAG_SUBSYSTEM, ss, TAG_CTLR, alias,
				TAG_GROUP, group);
			return -ENOENT;
		}
		iter = json_object();
		json_set_string(iter, TAG_NQN, ss);
		json_array_append_new(array, iter);
	}

	if (strlen(data) == 0) {
		if (create)
			return 0;
		sprintf(resp, "no data to update %s '%s' in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_CTLR, alias, TAG_GROUP, group);
		return -EINVAL;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	if (!create) {
		json_update_string(iter, new, TAG_NQN, value);
		rename_host_acl(ctx, group, ss,
				(char *) json_string_value(value));
	}

	json_update_int(iter, new, TAG_ALLOW_ALL, value);

	json_decref(new);

	sprintf(resp, "%s '%s' updated in %s '%s' in %s '%s'",
		TAG_SUBSYSTEM, ss, TAG_CTLR, alias, TAG_GROUP, group);

	return 0;
}

int del_subsys(void *context, char *group, char *alias, char *ss, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*ctlrs;
	json_t			*hosts;
	int			 ret;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (hosts)
		del_host_acl(hosts, ss);

	ctlrs = get_group_array(ctx, group, TAG_CTLRS);
	if (!ctlrs) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	ret = del_from_array(ctlrs, TAG_ALIAS, alias, TAG_SUBSYSTEMS, ss);
	if (ret) {
		sprintf(resp,
			"Unable to delete %s '%s' from %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_CTLR, alias, TAG_GROUP, group);
		return ret;
	}

	sprintf(resp,
		"%s '%s' deleted from %s '%s' in %s '%s'",
		TAG_SUBSYSTEM, ss, TAG_CTLR, alias, TAG_GROUP, group);

	return 0;
}

int set_acl(void *context, char *group, char *nqn, char *ss, char *data,
	    char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	int			 i;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_NQN, nqn, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_HOST, nqn, TAG_GROUP, group);
		return -ENOENT;
	}

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
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	json_update_string(iter, new, TAG_NQN, value);
	json_update_int(iter, new, TAG_ACCESS, value);

	json_decref(new);

	sprintf(resp, "%s '%s' updated in ACL for %s '%s' in %s '%s'",
		TAG_SUBSYSTEM, ss, TAG_HOST, nqn, TAG_GROUP, group);

	return 0;
}

int del_acl(void *context, char *group, char *nqn, char *ss, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	int			 ret;

	hosts = get_group_array(ctx, group, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	ret = del_from_array(hosts, TAG_NQN, nqn, TAG_ACL, ss);
	if (ret) {
		sprintf(resp,
			"%s '%s' not foud in %s '%s' in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_HOST, nqn, TAG_GROUP, group);
		return ret;
	}

	sprintf(resp,
		"%s '%s' deleted from ACL for %s '%s' in %s '%s'",
		TAG_SUBSYSTEM, ss, TAG_HOST, nqn, TAG_GROUP, group);

	return 0;
}

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
	json_object_set_new(iter, TAG_CTLRS, tmp);

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
	json_object_set_new(iter, TAG_CTLRS, tmp);

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
	int			 n;

	groups = json_object_get(ctx->root, TAG_GROUPS);

	n = sprintf(resp, "{\"%s\":[", TAG_GROUPS);
	resp += n;

	if (groups) {
		n = list_array(groups, TAG_NAME, resp);
		resp += n;
	}

	sprintf(resp, "]}");

	return 0;
}

int show_group(void *context, char *group, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*obj;
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

	n = sprintf(resp, "{\"%s\":\"%s\",", TAG_NAME, group);
	resp += n;

	n = _list_ctlr(context, group, resp);
	resp += n;

	n = sprintf(resp, ",");
	resp += n;

	resp[n] = 0;
	n = _list_host(context, group, resp);
	resp += n;

	sprintf(resp, "}");

	return 0;
}

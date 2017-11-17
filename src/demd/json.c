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

static int find_array_string(json_t *array, char *val)
{
	json_t			*iter;
	int			 i, n;

	n = json_array_size(array);
	for (i = 0; i < n; i++) {
		iter = json_array_get(array, i);
		if (!json_is_string(iter))
			continue;
		if (strcmp(json_string_value(iter), val) == 0)
			return i;
	}

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
			array_json_string(obj, p, i, n);
	}

	return p - resp;
}

static int filter_fabric(json_t *array, char *query, char *resp)
{
	json_t			*iter;
	json_t			*list;
	json_t			*obj;
	char			*p = resp;
	int			 i, j, n = 0, tmp, num_targets, num_ports;

	query += PARM_FABRIC_LEN;

	num_targets = json_array_size(array);

	for (i = 0; i < num_targets; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;

		list = json_object_get(iter, TAG_PORTIDS);
		if (!list || !json_is_array(list))
			continue;

		num_ports = json_array_size(list);

		for (j = 0; j < num_ports; j++) {
			obj = json_array_get(list, j);
			if (!json_is_object(obj))
				continue;

			obj = json_object_get(obj, TAG_TYPE);
			if (!obj || !json_is_string(obj))
				continue;
			if (strcmp(query, json_string_value(obj)))
				continue;

			obj = json_object_get(iter, TAG_ALIAS);
			if (!obj || !json_is_string(obj))
				continue;

			array_json_string(obj, p, n, tmp);
			n++;
		}
	}

	return p - resp;
}

static int filter_mode(json_t *array, char *query, char *resp)
{
	json_t			*iter;
	json_t			*obj;
	char			*p = resp;
	int			 i, n = 0, tmp, num_targets;

	query += PARM_MODE_LEN;

	num_targets = json_array_size(array);

	for (i = 0; i < num_targets; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;

		obj = json_object_get(iter, TAG_MGMT_MODE);
		if (!obj || !json_is_string(obj))
			continue;
		if (strcmp(query, json_string_value(obj)))
			continue;

		obj = json_object_get(iter, TAG_ALIAS);
		if (!obj || !json_is_string(obj))
			continue;

		array_json_string(obj, p, n, tmp);
		n++;
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

/* walk all subsystem allowed host list for host name changes */
static void rename_in_allowed_hosts(struct json_context *ctx, char *old,
				    char *new)
{
	json_t			*targets;
	json_t			*array;
	json_t			*tgt;
	json_t			*ss;
	json_t			*list;
	json_t			*obj;
	int			 idx;
	int			 i, j;
	int			 n, m;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets)
		return;

	n = json_array_size(targets);

	for (i = 0; i < n; i++) {
		tgt = json_array_get(targets, i);

		array = json_object_get(tgt, TAG_SUBSYSTEMS);
		if (!array)
			continue;

		m = json_array_size(array);

		for (j = 0; j < m; j++) {
			ss = json_array_get(array, i);

			list = json_object_get(ss, TAG_HOSTS);
			if (!list)
				continue;

			idx = find_array_string(list, old);
			if (idx >= 0) {
				obj = json_array_get(list, i);
				json_string_set(obj, new);
			}
		}
	}
}

/* walk all subsystem allowed host list for host deletions */
static void del_from_allowed_hosts(struct json_context *ctx, char *alias)
{
	json_t			*targets;
	json_t			*array;
	json_t			*tgt;
	json_t			*ss;
	json_t			*list;
	int			 idx;
	int			 i, j;
	int			 n, m;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets)
		return;

	n = json_array_size(targets);

	for (i = 0; i < n; i++) {
		tgt = json_array_get(targets, i);

		array = json_object_get(tgt, TAG_SUBSYSTEMS);
		if (!array)
			continue;

		m = json_array_size(array);

		for (j = 0; j < m; j++) {
			ss = json_array_get(array, i);

			list = json_object_get(ss, TAG_HOSTS);
			if (!list)
				continue;

			idx = find_array_string(list, alias);
			if (idx >= 0)
				json_array_remove(array, idx);
		}
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

static int _list_target(void *context, char *query, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	char			*p = resp;
	int			 n;

	targets = json_object_get(ctx->root, TAG_TARGETS);

	start_json_array(TAG_TARGETS, p, n);

	if (targets) {
		if (query == NULL)
			n = list_array(targets, TAG_ALIAS, p);
		else if (strncmp(query, URI_PARM_MODE, PARM_MODE_LEN) == 0)
			n = filter_mode(targets, query, p);
		else if (strncmp(query, URI_PARM_FABRIC, PARM_FABRIC_LEN) == 0)
			n = filter_fabric(targets, query, p);
		p += n;
	}

	end_json_array(p, n);

	return p - resp;
}

static int _list_host(void *context, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	char			*p = resp;
	int			 n;

	hosts = json_object_get(ctx->root, TAG_HOSTS);

	start_json_array(TAG_HOSTS, p, n);

	if (hosts) {
		n = list_array(hosts, TAG_ALIAS, p);
		p += n;
	}

	end_json_array(p, n);

	return p - resp;
}

/* GROUPS */

int add_group(void *context, char *group, char *resp)
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
	json_t			*iter;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;

	if (strlen(data) == 0)
		return -EINVAL;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		groups = json_array();
		json_object_set_new(ctx->root, TAG_GROUPS, groups);
	}

	if (group) {
		i = find_array(groups, TAG_NAME, group, &iter);
		if (i < 0) {
			sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
			return -ENOENT;
		}
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_NAME);
	if (!value) {
		sprintf(resp, "invalid json syntax");
		ret = -EINVAL;
		goto out;
	}

	strcpy(newname, json_string_value(value));
	if ((!group && *newname) || (strcmp(group, newname) != 0)) {
		i = find_array(groups, TAG_NAME, newname, &tmp);
		if (i >= 0) {
			sprintf(resp, "%s '%s' exists",
				TAG_GROUP, newname);
			ret = -EEXIST;
			goto out;
		}
	}
	if (group)
		json_update_string(iter, new, TAG_NAME, value);
	else {
		iter = json_object();
		json_set_string(iter, TAG_NAME, newname);
		json_array_append_new(groups, iter);

		tmp = json_array();
		json_object_set_new(iter, TAG_HOSTS, tmp);

		tmp = json_array();
		json_object_set_new(iter, TAG_TARGETS, tmp);
	}

	sprintf(resp, "%s '%s' %s ", TAG_GROUP, newname,
		(!group) ? "added" : "updated");
out:
	json_decref(new);

	return ret;
}

int set_group_member(void *context, char *group, char *data, char *tag,
		     char *parent_tag, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*parent;
	json_t			*iter;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 alias[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;

	if (strlen(data) == 0)
		return -EINVAL;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
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

	value = json_object_get(new, TAG_ALIAS);
	if (!value) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	sprintf(alias, "%.*s", (int) sizeof(alias) - 1,
		(char *) json_string_value(value));

	parent = json_object_get(ctx->root, parent_tag);
	if (!parent) {
		sprintf(resp, "%s '%s' not found", tag, alias);
		ret = -ENOENT;
		goto out;
	}

	i = find_array(parent, TAG_ALIAS, alias, &tmp);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", tag, alias);
		ret = -ENOENT;
		goto out;
	}

	parent = json_object_get(iter, parent_tag);
	if (!parent) {
		parent = json_array();
		json_object_set_new(iter, parent_tag, parent);
	}

	if (find_array_string(parent, alias) < 0)
		json_array_append_new(parent, json_string(alias));

	sprintf(resp, "%s '%s' updated", TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

int del_group_member(void *context, char *group, char *member, char *tag,
		     char *parent_tag, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*parent;
	json_t			*iter;
	int			 i;
	int			 ret = 0;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(groups, TAG_NAME, group, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	parent = json_object_get(iter, parent_tag);
	if (!parent) {
		sprintf(resp, "%s '%s' not found in %s '%s'", tag, member,
			TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array_string(parent, member);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'", tag, member,
			TAG_GROUP, group);
		return -ENOENT;
	}

	json_array_remove(parent, i);

	sprintf(resp, "%s '%s' updated", TAG_GROUP, group);

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
	int			 i;

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

	sprintf(resp, "%s", json_dumps(obj, 0));

	return 0;
}

/* HOSTS */

int add_host(void *context, char *host, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	json_t			*tmp;
	int			 i;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts) {
		hosts = json_array();
		json_object_set_new(ctx->root, TAG_HOSTS, hosts);
	} else {
		i = find_array(hosts, TAG_ALIAS, host, NULL);
		if (i >= 0) {
			sprintf(resp, "%s '%s' exists",	TAG_HOST, host);
			return -EEXIST;
		}
	}

	iter = json_object();
	json_set_string(iter, TAG_ALIAS, host);

	json_array_append_new(hosts, iter);

	sprintf(resp, "%s '%s' added", TAG_HOST, host);

	return 0;
}

int update_host(void *context, char *host, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 alias[MAX_STRING + 1];
	int			 ret = 0;
	int			 i;

	if (strlen(data) == 0)
		return -EINVAL;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts) {
		hosts = json_array();
		json_object_set_new(ctx->root, TAG_HOSTS, hosts);
	}

	if (host) {
		i = find_array(hosts, TAG_ALIAS, host, &iter);
		if (i < 0) {
			sprintf(resp, "%s '%s' not found", TAG_HOST, host);
			return -ENOENT;
		}
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_ALIAS);
	if (value) {
		strcpy(alias, (char *) json_string_value(value));

		if ((!host && *alias) || (strcmp(host, alias) != 0)) {
			i = find_array(hosts, TAG_ALIAS, alias, &tmp);
			if (i >= 0) {
				sprintf(resp, "%s '%s' exists",
					TAG_HOSTS, alias);
				ret = -EEXIST;
				goto out;
			}
			if (host) {
				json_update_string(iter, new, TAG_ALIAS, value);
				rename_in_allowed_hosts(ctx, host, alias);
			} else {
				iter = json_object();
				json_set_string(iter, TAG_ALIAS, alias);
				json_array_append_new(hosts, iter);
			}
		}
	} else
		strcpy(alias, host);

	json_update_string(iter, new, TAG_HOSTNQN, value);

	sprintf(resp, "%s '%s' %s ", TAG_HOST, alias,
		(!host) ? "added" : "updated");
out:
	json_decref(new);

	return ret;
}

int del_host(void *context, char *host, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter;
	int			 i;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_HOST, host);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_ALIAS, host, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_HOST, host);
		return -ENOENT;
	}

	json_array_remove(hosts, i);

	del_from_allowed_hosts(ctx, host);

	sprintf(resp, "%s '%s' deleted", TAG_HOST, host);

	return 0;
}

int list_host(void *context, char *resp)
{
	char			*p = resp;
	int			 n;

	n = sprintf(p, "{");
	p += n;

	n = _list_host(context, p);
	p += n;

	sprintf(p, "}");

	return 0;
}

static inline int match_string(json_t *item, char *str)
{
	return (json_is_string(item) &&
		(strcmp(str, json_string_value(item)) == 0));
}

static inline void add_host_subsys(json_t *alias, json_t *nqn, json_t *list)
{
	json_t			*obj;
	json_t			*tmp;

	obj = json_object();
	json_set_string(obj, TAG_ALIAS, json_string_value(alias));
	json_set_string(obj, TAG_SUBNQN, json_string_value(nqn));
	json_array_append_new(list, obj);
}

static void get_host_subsystems(struct json_context *ctx, char *host,
				json_t *parent)
{
	json_t			*targets;
	json_t			*alias;
	json_t			*subsys;
	json_t			*nqn;
	json_t			*any;
	json_t			*item;
	json_t			*target;
	json_t			*array;
	json_t			*list;
	json_t			*restricted;
	json_t			*shared;
	int			 i, j, k;
	int			 num_targets, num_subsys, num_hosts;

	shared = json_array();
	restricted = json_array();
	json_object_set_new(parent, TAG_SHARED, shared);
	json_object_set_new(parent, TAG_RESTRICTED, restricted);

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets)
		return;

	num_targets = json_array_size(targets);
	for (i = 0; i < num_targets; i++) {
		target = json_array_get(targets, i);
		alias = json_object_get(target, TAG_ALIAS);
		if (!alias)
			continue;
		json_get_array(target, TAG_SUBSYSTEMS, array);
		if (!array)
			continue;
		num_subsys = json_array_size(array);
		for (j = 0; j < num_subsys; j++) {
			subsys = json_array_get(array, j);
			nqn = json_object_get(subsys, TAG_SUBNQN);
			if (!nqn)
				continue;
			any = json_object_get(subsys, TAG_ALLOW_ANY);
			if (any && json_integer_value(any))
				add_host_subsys(alias, nqn, shared);
			else {
				list = json_object_get(subsys, TAG_HOSTS);
				num_hosts = json_array_size(list);
				for (k = 0; k < num_hosts; k++) {
					item = json_array_get(list, k);
					if (match_string(item, host))
						add_host_subsys(alias, nqn,
								restricted);
				}
			}
		}
	}
}

int show_host(void *context, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*obj;
	json_t			*host;
	int			 i;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "'%s' not found", TAG_HOSTS);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_HOST, alias);
		return -ENOENT;
	}

	host = json_copy(obj);

	get_host_subsystems(ctx, alias, host);

	sprintf(resp, "%s", json_dumps(host, 0));

	json_decref(host);

	return 0;
}

/* TARGET */
/* SUBSYSTEMS */

int set_subsys(void *context, char *alias, char *ss, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*obj;
	json_t			*iter;
	json_t			*array;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	char			 nqn[MAX_STRING + 1];
	json_error_t		 error;
	int			 i;
	int			 ret = 0;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found ", TAG_TARGET, alias);
		return -ENOENT;
	}

	json_get_array(obj, TAG_SUBSYSTEMS, array);
	if (!array)
		return -EINVAL;

	if (ss) {
		i = find_array(array, TAG_SUBNQN, ss, &iter);
		if (i < 0) {
			sprintf(resp, "%s '%s' not found in %s '%s'",
				TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
			return -ENOENT;
		}
		if (strlen(data) == 0) {
			sprintf(resp, "%s '%s' unchanged in %s '%s'",
				TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
			return 0;
		}
	} else {
		if (strlen(data) == 0) {
			sprintf(resp, "no data for update %s '%s' in %s '%s'",
				TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
			return -EINVAL;
		}
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	obj = json_object_get(new, TAG_SUBNQN);
	if (obj) {
		strcpy(nqn, (char *) json_string_value(obj));
		i = find_array(array, TAG_SUBNQN, nqn, NULL);
		if (i >= 0 && (!ss || strcmp(ss, nqn))) {
			sprintf(resp, "%s '%s' exists in %s '%s'",
				TAG_SUBSYSTEM, nqn, TAG_TARGET, alias);
			ret = -EEXIST;
			goto out;
		}
	}

	if (!ss) {
		if (!obj) {
			sprintf(resp, "no subsystem NQN provided");
			ret = -EINVAL;
			goto out;
		}

		iter = json_object();
		json_set_string(iter, TAG_SUBNQN, nqn);
		json_array_append_new(array, iter);
	} else {
		strcpy(nqn, ss);
		json_update_string(iter, new, TAG_SUBNQN, value);
	}

	json_update_int(iter, new, TAG_ALLOW_ANY, value);

	sprintf(resp, "%s '%s' %s in %s '%s'", TAG_SUBSYSTEM, nqn,
		(!ss) ? "added to" : "updated in", TAG_TARGET, alias);
out:
	json_decref(new);

	return ret;
}

int del_subsys(void *context, char *alias, char *ss, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	int			 ret;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	ret = del_from_array(targets, TAG_ALIAS, alias, TAG_SUBSYSTEMS, ss);
	if (ret) {
		sprintf(resp,
			"Unable to delete %s '%s' from %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return ret;
	}

	sprintf(resp, "%s '%s' deleted from %s '%s'",
		TAG_SUBSYSTEM, ss, TAG_TARGET, alias);

	return 0;
}

/* DRIVE */

int set_drive(void *context, char *alias, char *data, char *resp)
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

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_ALIAS, alias);
		return -ENOENT;
	}

	json_get_array(obj, TAG_NSIDS, array);
	if (!array) {
		array = json_array();
		json_object_set_new(array, TAG_NSIDS, obj);
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	obj = json_object_get(new, TAG_DEVID);
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
		obj = json_object_get(iter, TAG_DEVID);
		if (!obj || !json_is_string(obj))
			continue;
		if (strcmp(json_string_value(obj), val) == 0) {
			sprintf(resp,
				"%s '%s' exists in %s '%s'",
				TAG_DEVID, val, TAG_TARGET, alias);
			return -EEXIST;
		}
	}

	obj = json_object();
	json_set_string(obj, TAG_DEVID, val);
	json_array_append_new(array, obj);

	sprintf(resp, "Added %s '%s' to %s '%s'",
		TAG_DEVID, val, TAG_TARGET, alias);

	return 0;
}

int del_drive(void *context, char *alias, char *data, char *resp)
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

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found ", TAG_ALIAS, alias);
		return -ENOENT;
	}

	json_get_array(obj, TAG_NSIDS, array);
	if (!array) {
		array = json_array();
		json_object_set_new(array, TAG_NSIDS, obj);
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	obj = json_object_get(new, TAG_DEVID);
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
		obj = json_object_get(iter, TAG_DEVID);
		if (!obj || !json_is_string(obj))
			continue;
		if (strcmp(json_string_value(obj), val) == 0)
			break;
	}

	if (i == cnt) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_DEVID, val, TAG_TARGET, alias);
		return -ENOENT;
	}

	json_array_remove(array, i);

	sprintf(resp, "%s '%s' deleted from %s '%s'",
		TAG_DEVID, val, TAG_TARGET, alias);
	return 0;
}

/* PORTID */

int set_portid(void *context, char *target, int portid,
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

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_TARGETS, target);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, target, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, target);
		return -ENOENT;
	}

	json_get_array(obj, TAG_PORTIDS, array);
	if (!array)
		return -EINVAL;

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (strlen(data) == 0) {
		sprintf(resp, "no data to update %s '%d' in %s '%s'",
			TAG_PORTID, portid, TAG_TARGET, target);

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
			sprintf(resp, "no port id give for %s '%s'",
				TAG_TARGET, target);
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

	sprintf(resp, "%s '%d' updated in %s '%s'",
		TAG_PORTID, portid, TAG_TARGET, target);
out:
	json_decref(new);

	return ret;
}

int del_portid(void *context, char *alias, int portid,
	       char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	int			 ret;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGET);
		return -ENOENT;
	}

	ret = del_int_from_array(targets, TAG_ALIAS, alias, TAG_PORTIDS,
				 TAG_PORTID, portid);
	if (ret) {
		sprintf(resp,
			"Unable to delete %s '%d' from %s '%s'",
			TAG_PORTID, portid, TAG_TARGET, alias);
		return ret;
	}

	sprintf(resp, "%s '%d' deleted from %s '%s'",
		TAG_PORTID, portid, TAG_TARGET, alias);

	return 0;
}

/* NAMESPACE */

int set_ns(void *context, char *alias, char *ss, char *data,
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

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
		return -ENOENT;
	}

	json_get_array(subgroup, TAG_SUBSYSTEMS, array);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -ENOENT;
	}

	i = find_array(array, TAG_SUBNQN, ss, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
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

	json_update_int(iter, new, TAG_DEVID, value);
	json_update_int(iter, new, TAG_DEVNSID, value);

	json_decref(new);

	sprintf(resp, "%s %d updated in %s '%s' of %s '%s'",
		TAG_NSID, val, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);

	return 0;
}

int del_ns(void *context, char *alias, char *ss, int ns,
	   char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*subgroup;
	json_t			*array;
	int			 i;
	int			 ret;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found'", TAG_TARGET, alias);
		return -ENOENT;
	}

	array = json_object_get(subgroup, TAG_SUBSYSTEMS);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -ENOENT;
	}

	ret = del_int_from_array(array, TAG_SUBNQN, ss, TAG_NSIDS,
				 TAG_NSID, ns);
	if (ret) {
		sprintf(resp,
			"Unable to delete %s '%d' from %s '%s in %s '%s'",
			TAG_NSID, ns, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return ret;
	}

	sprintf(resp, "%s '%d' deleted from %s '%s' in %s '%s'",
		TAG_NSID, ns, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);

	return 0;
}

/* TARGET */

int del_target(void *context, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	json_t			*ss;
	int			 i, n, idx;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	idx = find_array(targets, TAG_ALIAS, alias, &iter);
	if (idx < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
		return -ENOENT;
	}

	array = json_object_get(iter, TAG_SUBSYSTEMS);
	if (array) {
		n = json_array_size(array);

		for (i = 0; i < n; i++) {
			obj = json_array_get(array, 0);
			ss = json_object_get(obj, TAG_SUBNQN);
			del_subsys(ctx, alias,
				   (char *) json_string_value(ss), resp);
		}
	}

	json_array_remove(targets, idx);

	sprintf(resp, "%s '%s' deleted", TAG_TARGET, alias);

	return 0;
}

int list_target(void *context, char *query, char *resp)
{
	char			*p = resp;
	int			 n;

	n = sprintf(p, "{");
	p += n;

	n = _list_target(context, query, p);
	p += n;

	sprintf(p, "}");

	return 0;
}

int set_interface(void *context, char *target, char *data, char *resp)
{
	struct json_context     *ctx = context;
	json_t			*targets;
	json_t			*new;
	json_t			*obj;
	json_t			*iter;
	json_t			*value;
	json_t			*tmp;
	int			 i;
	json_error_t		 error;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_TARGETS, target);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, target, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, target);
		return -ENOENT;
	}


	new = json_loads(data, JSON_DECODE_ANY, &error);

	iter = json_object();
	json_update_string(iter, new, TAG_IFFAMILY, value);
	json_update_string(iter, new, TAG_IFADDRESS, value);
	json_update_string(iter, new, TAG_IFPORT, value);

	json_object_set(obj, TAG_INTERFACE, iter);

	json_decref(new);
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

	json_get_array(parent, TAG_NSIDS, subgroup);
	if (!subgroup)
		return;

	new = json_object_get(newparent, TAG_NSIDS);
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
		json_update_int(subgroup, new, TAG_ALLOW_ANY, value);
	}
}

int add_target(void *context, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*iter;
	json_t			*tmp;
	int			 i;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		targets = json_array();
		json_object_set_new(ctx->root, TAG_TARGETS, targets);
	} else {
		i = find_array(targets, TAG_ALIAS, alias, &iter);
		if (i >= 0) {
			sprintf(resp, "%s '%s' exists", TAG_TARGET, alias);
			return -EEXIST;
		}
	}

	iter = json_object();
	json_set_string(iter, TAG_ALIAS, alias);

	tmp = json_array();
	json_object_set_new(iter, TAG_PORTIDS, tmp);

	tmp = json_array();
	json_object_set_new(iter, TAG_SUBSYSTEMS, tmp);

	json_array_append_new(targets, iter);

	sprintf(resp, "%s '%s' added", TAG_TARGET, alias);

	return 0;
}

int update_target(void *ctxt, char *target, char *data, char *resp)
{
	struct json_context	*ctx = ctxt;
	json_t			*targets;
	json_t			*iter;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	char			 alias[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;

	if (strlen(data) == 0)
		return -EINVAL;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		targets = json_array();
		json_object_set_new(ctx->root, TAG_TARGETS, targets);
	}

	if (target) {
		i = find_array(targets, TAG_ALIAS, target, &iter);
		if (i < 0) {
			sprintf(resp, "%s '%s' not forund", TAG_TARGET, target);
			return -ENOENT;
		}
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_ALIAS);
	if (value) {
		strcpy(alias, (char *) json_string_value(value));

		if ((!target && *alias) || (strcmp(target, alias) != 0)) {
			i = find_array(targets, TAG_ALIAS, alias, &tmp);
			if (i >= 0) {
				sprintf(resp, "%s '%s' exists",
					TAG_TARGET, alias);
				ret = -EEXIST;
				goto out;
			}

			if (target)
				json_update_string(iter, new, TAG_ALIAS, value);
			else {
				iter = json_object();
				json_set_string(iter, TAG_ALIAS, alias);

				tmp = json_array();
				json_object_set_new(iter, TAG_PORTIDS, tmp);

				tmp = json_array();
				json_object_set_new(iter, TAG_SUBSYSTEMS, tmp);

				json_array_append_new(targets, iter);
			}
		}
	} else if (!target) {
		sprintf(resp, "invalid json syntax");
		ret = -EINVAL;
		goto out;
	} else
		strcpy(alias, target);

	json_update_int(iter, new, TAG_REFRESH, value);
	json_update_string(iter, new, TAG_MGMT_MODE, value);

	value = json_object_get(new, TAG_INTERFACE);
	if (value)
		json_object_set(iter, TAG_INTERFACE, value);

	sprintf(resp, "%s '%s' %s ", TAG_TARGET, alias,
		(!target) ? "added" : "updated");
out:
	json_decref(new);

	return ret;
}

int show_target(void *context, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*obj;
	int			 i;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
		return -ENOENT;
	}

	sprintf(resp, "%s", json_dumps(obj, 0));
	return 0;
}

int set_acl(void *context, char *alias, char *ss, char *host_uri,
	    char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*hosts;
	json_t			*new;
	json_t			*subgroup;
	json_t			*obj;
	json_t			*value;
	json_t			*array;
	json_error_t		 error;
	char			 host[MAX_STRING + 1];
	int			 i;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s not found", TAG_HOSTS);
		return -ENOENT;
	}

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
		return -ENOENT;
	}

	json_get_array(subgroup, TAG_SUBSYSTEMS, array);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -ENOENT;
	}

	i = find_array(array, TAG_SUBNQN, ss, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -ENOENT;
	}

	array = json_object_get(obj, TAG_HOSTS);
	if (!array) {
		array = json_array();
		json_object_set_new(obj, TAG_HOSTS, array);
	}


	if (host_uri)
		strcpy(host, host_uri);
	else {
		new = json_loads(data, JSON_DECODE_ANY, &error);
		if (!new) {
			sprintf(resp, "invalid json syntax");
			return -EINVAL;
		}
		value = json_object_get(new, TAG_ALIAS);
		if (!value) {
			sprintf(resp, "invalid json syntax");
			json_decref(new);
			return -EINVAL;
		}
		strcpy(host, (char *) json_string_value(value));
		json_decref(new);
	}

	i = find_array(hosts, TAG_ALIAS, host, NULL);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_HOST, host);
		return -ENOENT;
	}

	if (find_array_string(array, host) >= 0) {
		sprintf(resp, "%s '%s' exists for %s '%s' in %s '%s'",
			TAG_HOST, host, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -EEXIST;
	}

	json_array_append_new(array, json_string(host));

	sprintf(resp, "%s '%s' added for %s '%s' in %s '%s'",
		TAG_HOST, host, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);

	return 0;
}

int del_acl(void *context, char *alias, char *ss, char *host, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*subgroup;
	json_t			*array;
	json_t			*obj;
	int			 i;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found'", TAG_TARGET, alias);
		return -ENOENT;
	}

	json_get_array(subgroup, TAG_SUBSYSTEMS, array);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_ALIAS, alias);
		return -ENOENT;
	}

	i = find_array(array, TAG_SUBNQN, ss, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -ENOENT;
	}

	array = json_object_get(obj, TAG_HOSTS);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -ENOENT;
	}

	i = find_array_string(array, host);
	if (i < 0) {
		sprintf(resp, "%s '%s' not allowed for %s '%s' in %s '%s'",
			TAG_HOST, host, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -ENOENT;
	}

	json_array_remove(array, i);

	sprintf(resp, "%s '%s' deleted from %s '%s' for %s '%s'",
		TAG_HOST, host, TAG_SUBSYSTEM, ss, TAG_ALIAS, alias);

	return 0;
}

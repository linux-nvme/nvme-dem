// SPDX-License-Identifier: DUAL GPL-2.0/BSD
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "mongoose.h"

static struct json_context *ctx;

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

static int strlen_array(json_t *array, char *tag)
{
	json_t			*iter;
	json_t			*obj;
	int			 i, cnt;
	int			 n = 0;

	cnt = json_array_size(array);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);
		if (!json_is_object(iter))
			continue;

		obj = json_object_get(iter, tag);
		if (obj && json_is_string(obj))
			n += strlen(json_string_value(obj)) + 4;
	}

	return n;
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
			  char *value, const char *subgroup, char *subnqn)
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

	i = find_array(array, TAG_SUBNQN, subnqn, NULL);
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

static void parse_config_file(void)
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
		store_json_config_file();
}

static inline int invalid_json_syntax(char *resp)
{
	sprintf(resp, "invalid json syntax");
	return -EINVAL;
}

/* walk all subsystem allowed host list for host name changes */
static void rename_in_allowed_hosts(char *old, char *new)
{
	json_t			*targets;
	json_t			*array;
	json_t			*tgt;
	json_t			*subsys;
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
			subsys = json_array_get(array, j);

			list = json_object_get(subsys, TAG_HOSTS);
			if (!list)
				continue;

			idx = find_array_string(list, old);
			if (idx >= 0) {
				obj = json_array_get(list, idx);
				json_string_set(obj, new);
			}
		}
	}
}

/* walk all subsystem allowed host list for host deletions */
static void del_from_allowed_hosts(char *alias)
{
	json_t			*targets;
	json_t			*array;
	json_t			*tgt;
	json_t			*subsys;
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
			subsys = json_array_get(array, i);

			list = json_object_get(subsys, TAG_HOSTS);
			if (!list)
				continue;

			idx = find_array_string(list, alias);
			if (idx >= 0)
				json_array_remove(array, idx);
		}
	}
}

/* command functions */

void store_json_config_file(void)
{
	json_t			*root = ctx->root;
	char			*filename = ctx->filename;
	int			 ret;

	ret = json_dump_file(root, filename, 2);
	if (ret)
		fprintf(stderr, "json_dump_file failed %d\n", ret);
}

struct json_context *get_json_context(void)
{
	return ctx;
}

void json_spinlock(void)
{
	pthread_spin_lock(&ctx->lock);
}

void json_spinunlock(void)
{
	pthread_spin_unlock(&ctx->lock);
}

int init_json(char *filename)
{
	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	strncpy(ctx->filename, filename, sizeof(ctx->filename));

	pthread_spin_init(&ctx->lock, PTHREAD_PROCESS_SHARED);

	parse_config_file();

	return 0;
}

void cleanup_json(void)
{
	json_decref(ctx->root);

	pthread_spin_destroy(&ctx->lock);

	free(ctx);
}

static int _list_target(char *query, char **resp, int offset)
{
	json_t			*targets;
	char			*p = *resp + offset;
	char			*new;
	int			 n;
	int			 cnt;
	int			 max = BODY_SIZE - 32;

	targets = json_object_get(ctx->root, TAG_TARGETS);

	start_json_array(TAG_TARGETS, p, n);

	cnt = n;

	if (targets) {
		n = strlen_array(targets, TAG_ALIAS);
		if (n > max) {
			max += n - BODY_SIZE;
			new = realloc(*resp, max);
			if (!new)
				goto out;
			*resp = new;
			p = new + cnt + offset;
		}

		if (query == NULL)
			n = list_array(targets, TAG_ALIAS, p);
		else if (strncmp(query, URI_PARM_MODE, PARM_MODE_LEN) == 0)
			n = filter_mode(targets, query, p);
		else if (strncmp(query, URI_PARM_FABRIC, PARM_FABRIC_LEN) == 0)
			n = filter_fabric(targets, query, p);

		p += n;
	}
out:
	end_json_array(p, n);

	return p - *resp;
}

/* GROUPS */

int add_json_group(char *group, char *resp)
{
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

int update_json_group(char *group, char *data, char *resp, char *_name)
{
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
		return invalid_json_syntax(resp);

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
	if (!new)
		return invalid_json_syntax(resp);

	value = json_object_get(new, TAG_NAME);
	if (!value) {
		ret = invalid_json_syntax(resp);
		goto out;
	}

	strcpy(newname, json_string_value(value));
	if ((!group && *newname) || (group && strcmp(group, newname) != 0)) {
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

	strcpy(_name, newname);

	sprintf(resp, "%s '%s' %s", TAG_GROUP, newname,
		(!group) ? "added" : "updated");
out:
	json_decref(new);

	return ret;
}

int set_json_group_member(char *group, char *data, char *alias, char *tag,
			  char *parent_tag, char *resp, char *_alias)
{
	json_t			*groups;
	json_t			*parent;
	json_t			*iter;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 member[MAX_STRING + 1];
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

	if (data) {
		if (strlen(data) == 0)
			return invalid_json_syntax(resp);

		new = json_loads(data, JSON_DECODE_ANY, &error);
		if (!new)
			return invalid_json_syntax(resp);

		value = json_object_get(new, TAG_ALIAS);
		if (!value) {
			json_decref(new);
			return invalid_json_syntax(resp);
		}

		sprintf(member, "%.*s", (int) sizeof(member) - 1,
			(char *) json_string_value(value));
		alias = member;
		json_decref(new);
	}

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

	strcpy(_alias, alias);

	sprintf(resp, "Added %s '%s' to %s '%s'", tag, alias,
		TAG_GROUP, group);
out:
	return ret;
}

int del_json_group_member(char *group, char *member, char *tag,
			  char *parent_tag, char *resp)
{
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

	sprintf(resp, "Removed %s '%s' from %s '%s'", tag, member,
		TAG_GROUP, group);

	return ret;
}

static void rename_in_groups(char *tag, char *member, char *alias)
{
	json_t			*groups;
	json_t			*group;
	json_t			*array;
	json_t			*item;
	int			 num_groups;
	int			 num_members;
	int			 i, j;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups)
		return;

	num_groups = json_array_size(groups);

	num_groups = json_array_size(groups);
	for (i = 0; i < num_groups; i++) {
		group = json_array_get(groups, i);
		array = json_object_get(group, tag);
		num_members = json_array_size(array);
		for (j = 0; j < num_members; j++) {
			item = json_array_get(array, j);
			if (json_is_string(item) &&
			    !strcmp(member, json_string_value(item))) {
				json_string_set(item, alias);
				break;
			}
		}
	}

}

static void del_from_groups(char *tag, char *member)
{
	json_t			*groups;
	json_t			*group;
	json_t			*array;
	json_t			*item;
	int			 num_groups;
	int			 num_members;
	int			 i, j;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups)
		return;

	num_groups = json_array_size(groups);
	for (i = 0; i < num_groups; i++) {
		group = json_array_get(groups, i);
		array = json_object_get(group, tag);
		num_members = json_array_size(array);
		for (j = 0; j < num_members; j++) {
			item = json_array_get(array, j);
			if (json_is_string(item) &&
			    !strcmp(member, json_string_value(item))) {
				json_array_remove(array, j);
				break;
			}
		}
	}
}

int del_json_group(char *group, char *resp)
{
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

int list_json_group(char **resp)
{
	json_t			*groups;
	char			*p = *resp;
	char			*new;
	int			 n, m;
	int			 max = BODY_SIZE - 32;
	int			 len;

	n = sprintf(p, "{");
	p += n;

	start_json_array(TAG_GROUPS, p, m);

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (groups) {
		len = strlen_array(groups, TAG_NAME);
		if (len > max) {
			max += len - BODY_SIZE;
			new = realloc(*resp, max);
			if (!new)
				goto out;
			*resp = new;
			p = new + n + m;
		}

		n = list_array(groups, TAG_NAME, p);
		p += n;
	}
out:
	end_json_array(p, n);

	sprintf(p, "}");

	return 0;
}

int show_json_group(char *group, char **resp)
{
	json_t			*groups;
	json_t			*obj;
	int			 i;

	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
		sprintf(*resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	i = find_array(groups, TAG_NAME, group, &obj);
	if (i < 0) {
		sprintf(*resp, "%s '%s' not found", TAG_GROUP, group);
		return -ENOENT;
	}

	free(*resp);

	*resp = json_dumps(obj, 0);

	return 0;
}

/* HOSTS */

int add_json_host(char *host, char *resp)
{
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

int get_json_host_nqn(char *host, char *nqn)
{
	json_t			*hosts;
	json_t			*iter;
	json_t			*obj;
	int			 i;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts)
		return -ENOENT;

	i = find_array(hosts, TAG_ALIAS, host, &iter);
	if (i < 0)
		return -ENOENT;

	obj = json_object_get(iter, TAG_HOSTNQN);
	if (!obj)
		return -EINVAL;

	strcpy(nqn, (char *) json_string_value(obj));

	return 0;
}

int update_json_host(char *host, char *data, char *resp, char *alias, char *nqn)
{
	json_t			*hosts;
	json_t			*iter = NULL;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	int			 ret = 0;
	int			 i;

	if (strlen(data) == 0)
		return invalid_json_syntax(resp);

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
	if (!new)
		return invalid_json_syntax(resp);

	value = json_object_get(new, TAG_ALIAS);
	if (value) {
		strcpy(alias, (char *) json_string_value(value));

		if ((!host && *alias) || (host && strcmp(host, alias))) {
			i = find_array(hosts, TAG_ALIAS, alias, &tmp);
			if (i >= 0) {
				sprintf(resp, "%s '%s' exists",
					TAG_HOSTS, alias);
				ret = -EEXIST;
				goto out;
			}
			if (host) {
				json_update_string(iter, new, TAG_ALIAS, value);
				rename_in_allowed_hosts(host, alias);
				rename_in_groups(TAG_HOSTS, host, alias);
			} else {
				iter = json_object();
				json_set_string(iter, TAG_ALIAS, alias);
				json_array_append_new(hosts, iter);
			}
		}
	} else if (!host) {
		ret = invalid_json_syntax(resp);
		goto out;
	} else
		strcpy(alias, host);

	if (unlikely(!iter)) {
		ret = -EFAULT;
		sprintf(resp, "Internal logic error");
		goto out;
	}

	json_update_string_ex(iter, new, TAG_HOSTNQN, value, nqn);

	sprintf(resp, "%s '%s' %s", TAG_HOST, alias,
		(!host) ? "added" : "updated");
out:
	json_decref(new);

	return ret;
}

int del_json_host(char *alias, char *resp, char *nqn)
{
	json_t			*hosts;
	json_t			*iter;
	json_t			*obj;
	int			 i;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts) {
		sprintf(resp, "%s '%s' not found", TAG_HOST, alias);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_ALIAS, alias, &iter);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_HOST, alias);
		return -ENOENT;
	}

	if (nqn) {
		obj = json_object_get(iter, TAG_HOSTNQN);
		if (obj)
			strcpy(nqn, json_string_value(obj));
	}

	json_array_remove(hosts, i);

	del_from_allowed_hosts(alias);

	del_from_groups(TAG_HOSTS, alias);

	sprintf(resp, "%s '%s' deleted", TAG_HOST, alias);

	return 0;
}

int list_json_host(char **resp)
{
	json_t			*hosts;
	char			*p = *resp;
	char			*new;
	int			 m, n;
	int			 max = BODY_SIZE - 32;
	int			 len;

	n = sprintf(p, "{");
	p += n;

	start_json_array(TAG_HOSTS, p, m);

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (hosts) {
		len = strlen_array(hosts, TAG_ALIAS);
		if (len > max) {
			max += len - BODY_SIZE;
			new = realloc(*resp, max);
			if (!new)
				goto out;
			*resp = new;
			p = new + n + m;
		}

		n = list_array(hosts, TAG_ALIAS, p);
		p += n;
	}
out:
	end_json_array(p, n);

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

static void get_host_subsystems(char *host, json_t *parent)
{
	struct target		*target;
	json_t			*targets;
	json_t			*alias;
	json_t			*subsys;
	json_t			*nqn;
	json_t			*any;
	json_t			*item;
	json_t			*iter;
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
		iter = json_array_get(targets, i);

		alias = json_object_get(iter, TAG_ALIAS);
		if (!alias)
			continue;

		json_get_array(iter, TAG_SUBSYSTEMS, array);
		if (!array)
			continue;

		target = find_target((char *) json_string_value(alias));
		if (target && target->group_member)
			if (!indirect_shared_group(target, host))
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

int show_json_host(char *alias, char **resp)
{
	json_t			*hosts;
	json_t			*obj;
	json_t			*host;
	int			 i;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts) {
		sprintf(*resp, "'%s' not found", TAG_HOSTS);
		return -ENOENT;
	}

	i = find_array(hosts, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(*resp, "%s '%s' not found", TAG_HOST, alias);
		return -ENOENT;
	}

	free(*resp);

	host = json_copy(obj);

	get_host_subsystems(alias, host);

	*resp = json_dumps(host, 0);

	json_decref(host);

	return 0;
}

/* TARGET */
/* SUBSYSTEMS */

int set_json_subsys(char *alias, char *subnqn, char *data, char *resp,
		    struct subsystem *subsys)
{
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
		sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
		return -ENOENT;
	}

	json_get_array(obj, TAG_SUBSYSTEMS, array);
	if (!array)
		return invalid_json_syntax(resp);

	if (subnqn) {
		i = find_array(array, TAG_SUBNQN, subnqn, &iter);
		if (i < 0) {
			sprintf(resp, "%s '%s' not found in %s '%s'",
				TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
			return -ENOENT;
		}
		if (strlen(data) == 0) {
			sprintf(resp, "%s '%s' unchanged in %s '%s'",
				TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
			return 0;
		}
	} else {
		if (strlen(data) == 0) {
			sprintf(resp, "no data for update %s '%s' in %s '%s'",
				TAG_SUBSYSTEM, (subnqn) ?: "<NULL>",
				TAG_TARGET, alias);
			return -EINVAL;
		}
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return invalid_json_syntax(resp);

	obj = json_object_get(new, TAG_SUBNQN);
	if (obj) {
		strcpy(nqn, (char *) json_string_value(obj));
		i = find_array(array, TAG_SUBNQN, nqn, NULL);
		if (i >= 0 && (!subnqn || strcmp(subnqn, nqn))) {
			sprintf(resp, "%s '%s' exists in %s '%s'",
				TAG_SUBSYSTEM, nqn, TAG_TARGET, alias);
			ret = -EEXIST;
			goto out;
		}
	}

	if (!subnqn) {
		if (!obj) {
			sprintf(resp, "no subsystem NQN provided");
			ret = -EINVAL;
			goto out;
		}

		iter = json_object();
		json_set_string(iter, TAG_SUBNQN, nqn);
		json_array_append_new(array, iter);
		strcpy(subsys->nqn, nqn);
	} else {
		strcpy(nqn, subnqn);
		strcpy(subsys->nqn, nqn);
		json_update_string_ex(iter, new, TAG_SUBNQN, value,
				      subsys->nqn);
	}

	json_update_int_ex(iter, new, TAG_ALLOW_ANY, value, subsys->access);

	sprintf(resp, "%s '%s' %s in %s '%s'", TAG_SUBSYSTEM, nqn,
		(!subnqn) ? "added to" : "updated in", TAG_TARGET, alias);
out:
	json_decref(new);

	return ret;
}

int del_json_subsys(char *alias, char *subnqn, char *resp)
{
	json_t			*targets;
	int			 ret;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	ret = del_from_array(targets, TAG_ALIAS, alias, TAG_SUBSYSTEMS, subnqn);
	if (ret) {
		sprintf(resp, "Unable to delete %s '%s' from %s '%s'",
			TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
		return ret;
	}

	sprintf(resp, "%s '%s' deleted from %s '%s'",
		TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);

	return 0;
}

/* set target lists */

int set_json_oob_nsdevs(struct target *target, char *data)
{
	struct nsdev		*nsdev, *next;
	json_t			*array;
	json_t			*targets;
	json_t			*tgt;
	json_t			*nsdevs;
	json_t			*new;
	json_t			*obj;
	json_t			*tmp;
	json_t			*iter;
	json_error_t		 error;
	char			*alias = target->alias;
	int			 i, cnt;
	int			 devid, nsid;
	int			 ret = -EINVAL;

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		print_err("invalid json syntax: %s", data);
		return -EINVAL;
	}

	targets = json_object_get(ctx->root, TAG_TARGETS);
	find_array(targets, TAG_ALIAS, alias, &tgt);

	json_get_array(tgt, TAG_NSDEVS, nsdevs);
	if (!nsdevs) {
		nsdevs = json_array();
		json_object_set_new(tgt, TAG_NSDEVS, nsdevs);
	} else
		json_array_clear(nsdevs);

	list_for_each_entry(nsdev, &target->device_list, node)
		nsdev->valid = 0;

	json_get_array(new, TAG_NSDEVS, array);
	if (!array)
		cnt = 0;
	else
		cnt = json_array_size(array);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		obj = json_object_get(iter, TAG_DEVID);
		if (!obj || !json_is_integer(obj)) {
			print_err("invalid json syntax: bad dev id");
			goto out;
		}

		devid = json_integer_value(obj);

		if (devid == NULL_BLK_DEVID)
			nsid = 0;
		else {
			obj = json_object_get(iter, TAG_DEVNSID);
			if (!obj || !json_is_integer(obj)) {
				print_err("invalid json syntax: bad dev nsid");
				goto out;
			}
			nsid = json_integer_value(obj);
		}

		list_for_each_entry(nsdev, &target->device_list, node)
			if (nsdev->nsdev == devid) {
				if (devid == NULL_BLK_DEVID)
					goto found;
				if (nsdev->nsid == nsid)
					goto found;
			}

		nsdev = malloc(sizeof(*nsdev));
		if (!nsdev) {
			print_err("unable to alloc nsdev");
			ret = -ENOMEM;
			goto out;
		}

		nsdev->nsdev = devid;
		nsdev->nsid = nsid;

		list_add_tail(&nsdev->node, &target->device_list);

		print_debug("Added %s %d:%d to %s '%s'",
			    TAG_DEVID, devid, nsid, TAG_TARGET, alias);

found:
		iter = json_object();

		json_set_int(iter, TAG_DEVID, devid);

		if (devid != NULL_BLK_DEVID)
			json_set_int(iter, TAG_DEVNSID, nsid);

		json_array_append_new(nsdevs, iter);

		nsdev->valid = 1;
	}

	list_for_each_entry_safe(nsdev, next, &target->device_list, node)
		if (!nsdev->valid) {
			print_err("Removing %s %d:%d from %s '%s'",
				  TAG_DEVID, nsdev->nsdev, nsdev->nsid,
				  TAG_TARGET, alias);

			list_del(&nsdev->node);
		}

	ret = 0;
out:
	json_decref(new);

	return ret;
}

int set_json_oob_interfaces(struct target *target, char *data)
{
	json_t			*new;
	json_t			*trtype, *tradr, *trfam;
	json_t			*iter;
	json_t			*array;
	json_t			*targets;
	json_t			*tgt;
	json_t			*ifaces;
	json_t			*tmp;
	json_error_t		 error;
	struct fabric_iface	*iface, *next;
	char			*alias = target->alias;
	int			 i, cnt;
	int			 ret;

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		print_err("invalid json syntax: %s", data);
		return -EINVAL;
	}

	targets = json_object_get(ctx->root, TAG_TARGETS);
	find_array(targets, TAG_ALIAS, alias, &tgt);

	json_get_array(tgt, TAG_INTERFACES, ifaces);
	if (!ifaces) {
		ifaces = json_array();
		json_object_set_new(tgt, TAG_INTERFACES, ifaces);
	} else
		json_array_clear(ifaces);

	list_for_each_entry(iface, &target->fabric_iface_list, node)
		iface->valid = 0;

	json_get_array(new, TAG_INTERFACES, array);
	if (!array)
		cnt = 0;
	else
		cnt = json_array_size(array);

	for (i = 0; i < cnt; i++) {
		iter = json_array_get(array, i);

		trtype = json_object_get(iter, TAG_TYPE);
		if (!trtype || !json_is_string(trtype)) {
			print_err("invalid json syntax. bad type %s '%s'",
				  TAG_TARGET, alias);
			continue;
		}

		trfam = json_object_get(iter, TAG_FAMILY);
		if (!trfam || !json_is_string(trfam)) {
			print_err("invalid json syntax. bad family %s '%s'",
				  TAG_TARGET, alias);
			continue;
		}

		tradr = json_object_get(iter, TAG_ADDRESS);
		if (!tradr || !json_is_string(tradr)) {
			print_err("invalid json syntax. bad address %s '%s'",
				  TAG_TARGET, alias);
			continue;
		}

		list_for_each_entry(iface, &target->fabric_iface_list, node)
			if (!strcmp(iface->type, json_string_value(trtype)) &&
			    !strcmp(iface->fam, json_string_value(trfam)) &&
			    !strcmp(iface->addr, json_string_value(tradr)))
				goto found;

		iface = malloc(sizeof(*iface));
		if (!iface) {
			print_err("unable to alloc iface");
			ret = -ENOMEM;
			goto out;
		}

		strcpy(iface->type, json_string_value(trtype));
		strcpy(iface->fam, json_string_value(trfam));
		strcpy(iface->addr, json_string_value(tradr));

		list_add_tail(&iface->node, &target->fabric_iface_list);

		print_debug("Added %s %s %s to %s '%s'",
			    iface->type, iface->fam, iface->addr,
			    TAG_TARGET, alias);

found:
		iter = json_object();
		json_set_string(iter, TAG_TYPE, iface->type);
		json_set_string(iter, TAG_FAMILY, iface->fam);
		json_set_string(iter, TAG_ADDRESS, iface->addr);

		json_array_append_new(ifaces, iter);

		iface->valid = 1;
	}

	list_for_each_entry_safe(iface, next, &target->fabric_iface_list, node)
		if (!iface->valid) {
			print_err("Removing %s %s %s from %s '%s'",
				  iface->type, iface->fam, iface->addr,
				  TAG_TARGET, alias);

			list_del(&iface->node);
		}

	ret = 0;
out:
	json_decref(new);
	return ret;
}

int set_json_inb_nsdev(struct target *target, struct nsdev *nsdev)
{
	json_t			*iter;
	json_t			*targets;
	json_t			*nsdevs;
	json_t			*tgt;
	json_t			*tmp;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	find_array(targets, TAG_ALIAS, target->alias, &tgt);
	if (!tgt)
		return -ENOENT;

	json_get_array(tgt, TAG_NSDEVS, nsdevs);
	if (!nsdevs) {
		nsdevs = json_array();
		json_object_set_new(tgt, TAG_NSDEVS, nsdevs);
	} else
		json_array_clear(nsdevs);

	iter = json_object();

	json_set_int(iter, TAG_DEVID, nsdev->nsdev);

	if (nsdev->nsdev != NULL_BLK_DEVID)
		json_set_int(iter, TAG_DEVNSID, nsdev->nsid);

	json_array_append_new(nsdevs, iter);

	return 0;
}

int set_json_inb_fabric_iface(struct target *target, struct fabric_iface *iface)
{
	json_t			*iter;
	json_t			*targets;
	json_t			*ifaces;
	json_t			*tgt;
	json_t			*tmp;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	find_array(targets, TAG_ALIAS, target->alias, &tgt);
	if (!tgt)
		return -ENOENT;

	json_get_array(tgt, TAG_INTERFACES, ifaces);
	if (!ifaces) {
		ifaces = json_array();
		json_object_set_new(tgt, TAG_INTERFACES, ifaces);
	} else
		json_array_clear(ifaces);

	iter = json_object();
	json_set_string(iter, TAG_TYPE, iface->type);
	json_set_string(iter, TAG_FAMILY, iface->fam);
	json_set_string(iter, TAG_ADDRESS, iface->addr);

	json_array_append_new(ifaces, iter);

	return 0;
}

/* PORTID */

int set_json_portid(char *target, int id, char *data, char *resp,
		    struct portid *portid)
{
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
		return invalid_json_syntax(resp);

	if (strlen(data) == 0) {
		sprintf(resp, "no data to update %s '%d' in %s '%s'",
			TAG_PORTID, id, TAG_TARGET, target);
		return -EINVAL;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return invalid_json_syntax(resp);

	if (!id) {
		tmp = json_object_get(new, TAG_PORTID);
		id = json_integer_value(tmp);
		if (!id) {
			sprintf(resp, "no port id give for %s '%s'",
				TAG_TARGET, target);
			ret = -EINVAL;
			goto out;
		}
	}

	i = find_array_int(array, TAG_PORTID, id, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_int(iter, TAG_PORTID, id);
		json_array_append_new(array, iter);
	}

	json_update_int_ex(iter, new, TAG_PORTID, value, portid->portid);
	json_update_string_ex(iter, new, TAG_TYPE, value, portid->type);
	json_update_string_ex(iter, new, TAG_ADDRESS, value, portid->address);
	json_update_string_ex(iter, new, TAG_FAMILY, value, portid->family);
	json_update_int_ex(iter, new, TAG_TRSVCID, value, portid->port_num);

	/* TODO do we use and need to save the treq
	 * json_update_string_ex(iter, new, TAG_TREQ, value, portid->treq);
	 */

	sprintf(resp, "%s '%d' updated in %s '%s'",
		TAG_PORTID, id, TAG_TARGET, target);
out:
	json_decref(new);

	return ret;
}

int del_json_portid(char *alias, int portid, char *resp)
{
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

int set_json_ns(char *alias, char *subnqn, char *data, char *resp,
		struct ns *ns)
{
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
	int			 ret = -ENOENT;
	int			 i;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s not found", TAG_TARGETS);
		goto out;
	}

	i = find_array(targets, TAG_ALIAS, alias, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
		goto out;
	}

	json_get_array(subgroup, TAG_SUBSYSTEMS, array);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
		goto out;
	}

	i = find_array(array, TAG_SUBNQN, subnqn, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
		goto out;
	}


	json_get_array(obj, TAG_NSIDS, array);
	if (!array)
		return invalid_json_syntax(resp);

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return invalid_json_syntax(resp);

	obj = json_object_get(new, TAG_NSID);
	if (!obj) {
		json_decref(new);
		return invalid_json_syntax(resp);
	}

	val = json_integer_value(obj);
	ns->nsid = val;

	i = find_array_int(array, TAG_NSID, val, &iter);
	if (i < 0) {
		iter = json_object();
		json_set_int(iter, TAG_NSID, val);
		json_array_append_new(array, iter);
	}

	json_update_int_ex(iter, new, TAG_DEVID, value, ns->devid);
	json_update_int_ex(iter, new, TAG_DEVNSID, value, ns->devns);

	json_decref(new);

	sprintf(resp, "%s %d updated in %s '%s' of %s '%s'",
		TAG_NSID, val, TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
	ret = 0;
out:
	return ret;
}

int del_json_ns(char *alias, char *subnqn, int ns, char *resp)
{
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
			TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
		return -ENOENT;
	}

	ret = del_int_from_array(array, TAG_SUBNQN, subnqn, TAG_NSIDS,
				 TAG_NSID, ns);
	if (ret) {
		sprintf(resp,
			"Unable to delete %s '%d' from %s '%s in %s '%s'",
			TAG_NSID, ns, TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
		return ret;
	}

	sprintf(resp, "%s '%d' deleted from %s '%s' in %s '%s'",
		TAG_NSID, ns, TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);

	return 0;
}

/* TARGET */

int del_json_target(char *alias, char *resp)
{
	json_t			*targets;
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	json_t			*subnqn;
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
			subnqn = json_object_get(obj, TAG_SUBNQN);
			del_json_subsys(alias,
					(char *) json_string_value(subnqn),
					resp);
		}
	}

	json_array_remove(targets, idx);

	del_from_groups(TAG_TARGETS, alias);

	sprintf(resp, "%s '%s' deleted", TAG_TARGET, alias);

	return 0;
}

int list_json_target(char *query, char **resp)
{
	char			*p = *resp;
	int			 n, m;

	n = sprintf(p, "{");
	p += n;

	m = _list_target(query, resp, n);
	p = *resp + n + m - 1;

	sprintf(p, "}");

	return 0;
}

int set_json_inb_interface(char *alias, char *data, char *resp,
			   union sc_iface *iface)
{
	json_t			*targets;
	json_t			*new;
	json_t			*obj;
	json_t			*newobj;
	json_t			*iter;
	json_t			*value;
	json_t			*tmp;
	int			 i;
	json_error_t		 error;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_TARGETS, alias);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return invalid_json_syntax(resp);

	newobj = json_object_get(new, TAG_INTERFACE);
	if (!newobj) {
		json_decref(new);
		return invalid_json_syntax(resp);
	}

	iter = json_object();
	json_update_string_ex(iter, newobj, TAG_TYPE, value,
			      iface->inb.portid->type);
	json_update_string_ex(iter, newobj, TAG_FAMILY, value,
			      iface->inb.portid->family);
	json_update_string_ex(iter, newobj, TAG_ADDRESS, value,
			      iface->inb.portid->address);
	json_update_int_ex(iter, newobj, TAG_TRSVCID, value,
			   iface->inb.portid->port_num);

	print_debug("Added %s:%d", iface->inb.portid->address,
		    iface->inb.portid->port_num);

	json_object_set(obj, TAG_INTERFACE, iter);

	json_decref(new);

	return 0;
}
int set_json_oob_interface(char *alias, char *data, char *resp,
			   union sc_iface *iface)
{
	json_t			*targets;
	json_t			*new;
	json_t			*obj;
	json_t			*newobj;
	json_t			*iter;
	json_t			*value;
	json_t			*tmp;
	int			 i;
	json_error_t		 error;
	char			 port[MAX_STRING + 1];

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(resp, "%s '%s' not found", TAG_TARGETS, alias);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
		return -ENOENT;
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return invalid_json_syntax(resp);

	newobj = json_object_get(new, TAG_INTERFACE);
	if (!newobj) {
		json_decref(new);
		return invalid_json_syntax(resp);
	}

	iter = json_object();
	json_update_string(iter, newobj, TAG_IFFAMILY, value);
	json_update_string_ex(iter, newobj, TAG_IFADDRESS, value,
			      iface->oob.address);
	json_update_string_ex(iter, newobj, TAG_IFPORT, value, port);

	iface->oob.port = atoi(port);

	print_debug("Added %s:%d", iface->oob.address, iface->oob.port);

	json_object_set(obj, TAG_INTERFACES, iter);

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

int add_json_target(char *alias, char *resp)
{
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

int update_json_target(char *alias, char *data, char *resp,
		       struct target *target)
{
	json_t			*targets;
	json_t			*iter = NULL;
	json_t			*new;
	json_t			*value;
	json_t			*obj;
	json_t			*newobj;
	json_t			*tmp;
	json_error_t		 error;
	char			 buf[MAX_STRING + 1];
	char			 mode[MAX_STRING + 1];
	char			*newalias;
	int			 i;
	int			 ret = 0;

	if (strlen(data) == 0)
		return invalid_json_syntax(resp);

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		targets = json_array();
		json_object_set_new(ctx->root, TAG_TARGETS, targets);
	}

	if (alias) {
		i = find_array(targets, TAG_ALIAS, alias, &iter);
		if (i < 0) {
			sprintf(resp, "%s '%s' not found", TAG_TARGET, alias);
			return -ENOENT;
		}
	}

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new)
		return invalid_json_syntax(resp);

	value = json_object_get(new, TAG_ALIAS);
	if (value) {
		strcpy(buf, (char *) json_string_value(value));

		if ((!alias && *buf) || (alias && strcmp(alias, buf) != 0)) {
			i = find_array(targets, TAG_ALIAS, buf, &tmp);
			if (i >= 0) {
				sprintf(resp, "%s '%s' exists",
					TAG_TARGET, buf);
				ret = -EEXIST;
				goto out;
			}

			if (alias) {
				json_update_string(iter, new, TAG_ALIAS, value);

				newalias = (char *) json_string_value(value);
				rename_in_groups(TAG_TARGETS, alias, newalias);
			} else {
				iter = json_object();
				json_set_string(iter, TAG_ALIAS, buf);

				tmp = json_array();
				json_object_set_new(iter, TAG_PORTIDS, tmp);

				tmp = json_array();
				json_object_set_new(iter, TAG_SUBSYSTEMS, tmp);

				json_array_append_new(targets, iter);
			}
		}
	} else if (alias)
		strcpy(buf, alias);
	else {
		ret = invalid_json_syntax(resp);
		goto out;
	}

	strncpy(target->alias, buf, MAX_ALIAS_SIZE);
	target->alias[MAX_ALIAS_SIZE] = 0;
	memset(mode, 0, sizeof(mode));

	if (unlikely(!iter)) {
		ret = -EFAULT;
		sprintf(resp, "Internal logic error");
		goto out;
	}

	json_update_int_ex(iter, new, TAG_REFRESH, value, target->refresh);
	json_update_string_ex(iter, new, TAG_MGMT_MODE, value, mode);
	target->mgmt_mode = get_mgmt_mode(mode);

	if (target->mgmt_mode == LOCAL_MGMT)
		json_object_del(iter, TAG_INTERFACE);
	else {
		union sc_iface	*iface = &target->sc_iface;

		newobj = json_object_get(new, TAG_INTERFACE);
		if (!newobj)
			goto out2;

		obj = json_object_get(iter, TAG_INTERFACE);
		if (!obj) {
			obj = json_object();
			json_object_set_new(iter, TAG_INTERFACE, obj);
		}

		if (target->mgmt_mode == OUT_OF_BAND_MGMT) {
			json_object_del(obj, TAG_TYPE);
			json_object_del(obj, TAG_FAMILY);
			json_object_del(obj, TAG_ADDRESS);
			json_object_del(obj, TAG_TRSVCID);
			json_update_string_ex(obj, newobj, TAG_IFFAMILY,
					      value, iface->oob.address);
			json_update_string_ex(obj, newobj, TAG_IFADDRESS,
					      value, iface->oob.address);
			json_update_int_ex(obj, newobj, TAG_IFPORT,
					   value, iface->oob.port);
		} else {
			json_object_del(obj, TAG_IFFAMILY);
			json_object_del(obj, TAG_IFADDRESS);
			json_object_del(obj, TAG_IFPORT);

			json_update_string_ex(obj, newobj, TAG_TYPE, value,
					      iface->inb.portid->type);
			json_update_string_ex(obj, newobj, TAG_FAMILY, value,
					      iface->inb.portid->family);
			json_update_string_ex(obj, newobj, TAG_ADDRESS, value,
					      iface->inb.portid->address);
			json_update_int_ex(obj, newobj, TAG_TRSVCID, value,
					   iface->inb.portid->port_num);
		}
	}
out2:
	sprintf(resp, "%s '%s' %s", TAG_TARGET, buf,
		(!alias) ? "added" : "updated");
out:
	json_decref(new);

	return ret;
}

int show_json_target(char *alias, char **resp)
{
	json_t			*targets;
	json_t			*obj;
	int			 i;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		sprintf(*resp, "%s not found", TAG_TARGETS);
		return -ENOENT;
	}

	i = find_array(targets, TAG_ALIAS, alias, &obj);
	if (i < 0) {
		sprintf(*resp, "%s '%s' not found", TAG_TARGET, alias);
		return -ENOENT;
	}

	free(*resp);

	*resp = json_dumps(obj, 0);

	return 0;
}

int set_json_acl(char *tgt, char *subnqn, char *alias, char *data,
		 char *resp, char *newalias, char *hostnqn)
{
	json_t			*targets;
	json_t			*hosts;
	json_t			*new;
	json_t			*subgroup;
	json_t			*obj;
	json_t			*value;
	json_t			*array;
	json_t			*host;
	json_error_t		 error;
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

	i = find_array(targets, TAG_ALIAS, tgt, &subgroup);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_TARGET, tgt);
		return -ENOENT;
	}

	json_get_array(subgroup, TAG_SUBSYSTEMS, array);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, subnqn, TAG_TARGET, tgt);
		return -ENOENT;
	}

	i = find_array(array, TAG_SUBNQN, subnqn, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, subnqn, TAG_TARGET, tgt);
		return -ENOENT;
	}

	array = json_object_get(obj, TAG_HOSTS);
	if (!array) {
		array = json_array();
		json_object_set_new(obj, TAG_HOSTS, array);
	}


	if (alias)
		strcpy(newalias, alias);
	else {
		new = json_loads(data, JSON_DECODE_ANY, &error);
		if (!new)
			return invalid_json_syntax(resp);

		value = json_object_get(new, TAG_ALIAS);
		if (!value) {
			json_decref(new);
			return invalid_json_syntax(resp);
		}

		strcpy(newalias, (char *) json_string_value(value));

		json_decref(new);
	}

	i = find_array(hosts, TAG_ALIAS, newalias, &host);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_HOST, newalias);
		return -ENOENT;
	}

	if (find_array_string(array, newalias) >= 0) {
		sprintf(resp, "%s '%s' exists for %s '%s' in %s '%s'",
			TAG_HOST, newalias, TAG_SUBSYSTEM, subnqn,
			TAG_TARGET, tgt);
		return -EEXIST;
	}

	value = json_object_get(host, TAG_HOSTNQN);
	if (unlikely(!value)) {
		sprintf(resp, "%s '%s' has no hostnqn", TAG_HOST, newalias);
		return -EINVAL;
	}

	strcpy(hostnqn, json_string_value(value));

	json_array_append_new(array, json_string(newalias));

	sprintf(resp, "%s '%s' added for %s '%s' in %s '%s'",
		TAG_HOST, newalias, TAG_SUBSYSTEM, subnqn, TAG_TARGET, tgt);

	return 0;
}

int del_json_acl(char *alias, char *subnqn, char *host, char *resp)
{
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
			TAG_SUBSYSTEM, subnqn, TAG_ALIAS, alias);
		return -ENOENT;
	}

	i = find_array(array, TAG_SUBNQN, subnqn, &obj);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
		return -ENOENT;
	}

	array = json_object_get(obj, TAG_HOSTS);
	if (!array) {
		sprintf(resp, "%s '%s' not found in %s '%s'",
			TAG_SUBSYSTEM, subnqn, TAG_TARGET, alias);
		return -ENOENT;
	}

	i = find_array_string(array, host);
	if (i < 0) {
		sprintf(resp, "%s '%s' not allowed for %s '%s' in %s '%s'",
			TAG_HOST, host, TAG_SUBSYSTEM, subnqn,
			TAG_TARGET, alias);
		return -ENOENT;
	}

	json_array_remove(array, i);

	sprintf(resp, "%s '%s' deleted from %s '%s' for %s '%s'",
		TAG_HOST, host, TAG_SUBSYSTEM, subnqn, TAG_ALIAS, alias);

	return 0;
}

/* MISC */

int update_signature(char *data, char *resp)
{
	json_t			*request;
	json_t			*old;
	json_t			*new;
	json_error_t		 error;
	FILE			*fd;
	char			*buf;
	int			 len;

	if (strlen(data) == 0)
		goto invalid;

	request = json_loads(data, JSON_DECODE_ANY, &error);
	if (!request)
		goto invalid;

	old = json_object_get(request, TAG_OLD);
	if (!old || !json_is_string(old))
		goto invalid;

	new = json_object_get(request, TAG_NEW);
	if (!new || !json_is_string(new))
		goto invalid;

	len = 6 + strlen(json_string_value(old));
	buf = malloc(len + 1);
	if (!buf) {
		sprintf(resp, "unable to allocate memory for update");
		return -ENOMEM;
	}

	sprintf(buf, "Basic %s", json_string_value(old));
	if (mg_vcmp(s_signature, buf)) {
		free(buf);
		goto invalid;
	}

	free(buf);

	fd = fopen(SIGNATURE_FILE, "w");
	if (!fd) {
		sprintf(resp, "unable to update");
		return -EPERM;
	}

	fputs((char *) json_string_value(new), fd);
	fclose(fd);

	len = 6 + strlen(json_string_value(new));
	buf = malloc(len + 1);
	if (!buf) {
		sprintf(resp, "unable to allocate memory for update");
		return -ENOMEM;
	}

	if (s_signature == &s_signature_user)
		free((char *) s_signature->p);

	sprintf(buf, "Basic %s", json_string_value(new));

	s_signature_user = mg_mk_str_n(buf, len);
	s_signature = &s_signature_user;

	free(buf);

	sprintf(resp, "update successful");
	return 0;
invalid:
	return invalid_json_syntax(resp);
}

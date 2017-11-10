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

int find_array(json_t *array, const char *tag, char *val,
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

static json_t *get_host_array(struct json_context *ctx, char *host, char *tag)
{
	int			 idx;
	json_t			*hosts;
	json_t			*parent;
	json_t			*array;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts)
		return NULL;

	idx = find_array(hosts, TAG_NAME, host, &parent);
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
static void rename_host_acl(struct json_context *ctx, char *old, char *new)
{
	json_t			*hosts;
	json_t			*array;
	json_t			*iter;
	json_t			*obj;
	json_t			*tmp;
	int			 idx;
	int			 i;
	int			 cnt;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
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

static int _list_target(void *context, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	char			*p = resp;
	int			 n;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	
	start_json_array(TAG_TARGETS, p, n);

	if (targets) {
		n = list_array(targets, TAG_ALIAS, p);
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

int update_group(void *context, char *group, char *data, char *resp, int c_flag)
{
	struct json_context	*ctx = context;
	json_t			*groups;
	json_t			*iter1 = NULL;
	json_t			*iter2 = NULL;
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

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}
	
	value = json_object_get(new, TAG_NAME);
	if (value) {
		strcpy(newname, json_string_value(value));
	}

	if(c_flag == MUST_EXIST) {
		groups = json_object_get(ctx->root, TAG_GROUPS);
		if(!groups) {
			sprintf(resp, "%s '%s' not found", TAG_GROUPS, group);
			return -ENOENT;
		}
		i = find_array(groups, TAG_NAME, group, &iter1);
		if (i < 0) {
			sprintf(resp, "%s '%s' not found", TAG_GROUP, group);
			return -ENOENT;
		}
		if(value && (strcmp(group, newname) != 0)) {
			i = find_array(groups, TAG_NAME, newname, &iter2);
			if (i >= 0) {
				sprintf(resp, "%s '%s' exists", 
					TAG_GROUP, newname);
				ret = -EEXIST;
				goto out;
			}
			json_update_string(iter1, new, TAG_NAME, value);	
		}
	}
	else {
		if (!value) {
                        sprintf(resp, "invalid json syntax, no %s defined",
                                TAG_NAME);
                        ret = -EINVAL;
                        goto out;
                }
                i = -ENOENT;
                groups = json_object_get(ctx->root, TAG_GROUPS);
                if (!groups) {
                        groups = json_array();
                        json_object_set_new(ctx->root, TAG_GROUPS, groups);
                }
                else
                        i = find_array(groups, TAG_NAME, newname, &iter1);

                if(i == -ENOENT) {
                        iter1 = json_object();
                        json_set_string(iter1, TAG_NAME, newname);
                        json_array_append_new(groups, iter1);
		        
			tmp = json_array();
		        json_object_set_new(iter1, TAG_TARGETS, tmp);
	
			tmp = json_array();
		        json_object_set_new(iter1, TAG_HOSTS, tmp);
                }
	}


	sprintf(resp, "%s '%s' updated", TAG_GROUP, group);
out:
	json_decref(new);

	return ret;
}

int set_group_host(void *context, char *group, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t                  *groups;
	json_t                  *new;
	json_t                  *hosts;
        json_t                  *value;
	json_t                  *iter1;
	json_error_t             error;
	char                     host_nqn[MAX_STRING + 1];
	int			 i;

	
	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
                        sprintf(resp, "%s '%s' not found", TAG_GROUPS, group);
                        return -ENOENT;
        }
	
	i = find_array(groups, TAG_NAME, group, &iter1);
	if (i == -ENOENT) {
                  sprintf(resp, "%s '%s' does not exists",
			  TAG_GROUP, group);
		  return -ENOENT;
	}	

	hosts = json_object_get(iter1, TAG_HOSTS);
	if (!hosts) {
		hosts = json_array();
		json_object_set_new(iter1, TAG_HOSTS, hosts);
	}
		
	new = json_loads(data, JSON_DECODE_ANY, &error);
        if (!new) {
                sprintf(resp, "invalid json syntax");
                return -EINVAL;
        }

        value = json_object_get(new, TAG_HOSTNQN);
        if (value) {
                strcpy(host_nqn, (char *)json_string_value(value));
        }
	json_array_append_new(hosts, value);	

	return 0;	
}

int set_group_target(void *context, char *group, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t                  *groups;
	json_t                  *new;
	json_t                  *targets;
        json_t                  *value;
	json_t                  *iter1;
	json_error_t             error;
	char                     tgt_alias[MAX_STRING + 1];
	int			 i;
	groups = json_object_get(ctx->root, TAG_GROUPS);
	if (!groups) {
                        sprintf(resp, "%s '%s' not found", TAG_GROUPS, group);
                        return -ENOENT;
        }
	
	i = find_array(groups, TAG_NAME, group, &iter1);
	if (i == -ENOENT) {
                  sprintf(resp, "%s '%s' does not exists",
			  TAG_GROUP, group);
		  return -ENOENT;
	}	

	targets = json_object_get(iter1, TAG_TARGETS);
	if (!targets) {
		targets = json_array();
		json_object_set_new(iter1, TAG_TARGETS, targets);
	}
		
	new = json_loads(data, JSON_DECODE_ANY, &error);
        if (!new) {
                sprintf(resp, "invalid json syntax");
                return -EINVAL;
        }

        value = json_object_get(new, TAG_ALIAS);
        if (value) {
                strcpy(tgt_alias, (char *)json_string_value(value));
        }
	
	json_array_append_new(targets, value);	
	return 0;
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
//	char			*p = resp;
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

//	n = sprintf(p, "{" JSSTR ",", TAG_NAME, group);
//	p += n;
	
//	n = _list_target(context, p);
//	p += n;

//	n = sprintf(p, ",");
//	p += n;

//	n = _list_host(context, p);
//	p += n;

//	sprintf(p, "}");
	sprintf(resp, "%s", json_dumps(obj, 0));

	return 0;
}

/* HOST INTERFACES */

int set_transport(void *context, char *host, char *data, char *resp)
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

	array = json_object_get(iter, TAG_TRANSPORT);
	if (!array) {
		array = json_array();
		json_object_set_new(iter, TAG_TRANSPORT, array);
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

		sprintf(resp, "%s exists for %s '%s'",
			TAG_INTERFACE, TAG_HOST, host );

		ret = -EEXIST;
		goto out;
	}

	obj = json_object();

	json_set_string(obj, TAG_TYPE, trtype);
	json_set_string(obj, TAG_FAMILY, adrfam);
	json_set_string(obj, TAG_ADDRESS, traddr);

	json_array_append_new(array, obj);

	sprintf(resp, "%s added to %s '%s'", TAG_TRANSPORT, TAG_HOST, host);
	ret = 0;
out:
	json_decref(new);

	return ret;
}

int del_transport(void *context, char *host, char *data, char *resp)
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

	array = json_object_get(iter, TAG_TRANSPORT);
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

		sprintf(resp, "%s deleted from %s '%s",
			TAG_TRANSPORT, TAG_HOST, host);
		ret = 0;
		goto out;
	}
notfound:
	sprintf(resp, "%s does not exist for %s '%s' ",
		TAG_TRANSPORT, TAG_HOST, host);
	ret = -ENOENT;

out:
	json_decref(new);

	return ret;
}

/* HOSTS */

int add_to_hosts(void *context, char *host, char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*parent;
//	json_t			*array;
//	json_t			*iter;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
//	int			 i;
	int			 idx;
	int			 ret = 0;
	int			 added = 0;
	int			 create_flag = 0;
	int			 exists = 0;

// We know we do not have host data in this case ... see code flow
// We will just have data and we need to get HOST alias from data

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (!hosts)
		return -1;
	
	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	value = json_object_get(new, TAG_ALIAS);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_ALIAS);
		ret = -EINVAL;
		goto out;
	}
	strcpy(newname, (char *) json_string_value(value));
	
	idx = find_array(hosts, TAG_ALIAS, host, &parent);
	if (idx < 0) {
		// entry alias is not present in the list so add it
//		json_update_string(hosts, newname, TAG_ALIAS, value);
		
	}
	else {
		// entry Alias exists
	}		
//	if((strcmp(host,"") == 0)) {
//		create_flag = 1;
//		value = json_object_get(new, TAG_ALIAS);
//	}	
//	else {
//		create_flag = 0;
//	}

	if ( (create_flag == 0) && !exists )
		return -EINVAL;

	value = json_object_get(new, TAG_ALIAS);
	if (!value) {
		sprintf(resp, "invalid json syntax, no %s defined",
			TAG_ALIAS);
		ret = -EINVAL;
		goto out;
	}
	
	idx = find_array(hosts, TAG_ALIAS, newname, &parent);
	if (idx < 0)
              return -1;
	
	json_update_string(hosts, new, TAG_ALIAS, value);
	json_update_string(hosts, new, TAG_HOSTNQN, value);
	sprintf(resp, "%s '%s' %s %s '%s'", TAG_HOST, newname,
		added ? "added to" : "updated in ", TAG_HOST, host);
out:
	json_decref(new);

	return ret;
}

int add_a_host(void *context, char *host, char *resp)
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
	}
	else {
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

int update_host(void *context, char *host, char *data, char *resp, int c_flag)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*iter1;
	json_t			*iter2;
	json_t			*tmp;
	json_t			*new;
	json_t			*value;
	json_error_t		 error;
	char			 hostnqn[MAX_STRING + 1];
	char			 hostalias[MAX_STRING + 1];
	int			 ret = 0;
	int			 i;

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}
	value = json_object_get(new, TAG_ALIAS);
	if (value) {
		strcpy(hostalias, (char *)json_string_value(value));
	}
	
	if ( c_flag == MUST_EXIST) {
		// must exist; host is part of URI and must exist
		hosts = json_object_get(ctx->root, TAG_HOSTS);
		if (!hosts) {
			return -ENOENT;
		}
		i = find_array(hosts, TAG_ALIAS, host, &iter1);
                if (i == -ENOENT) {
                        sprintf(resp, "%s '%s' does not exists", 
                                TAG_HOST, host);
                        return -ENOENT;
                }
		if(value && (strcmp(host, hostalias) != 0)) {
			i = find_array(hosts, TAG_ALIAS, hostalias, &iter2);
			if (i != -ENOENT) {
				sprintf(resp, "%s '%s' exists", 
					TAG_HOSTS, hostalias);
				ret = -EEXIST;
				goto out;	
			}
			json_update_string(iter1, new, TAG_ALIAS, value);
		}		
	}
	else {
		//can creat; host is not part of URI
		if (!value) {
			sprintf(resp, "invalid json syntax, no %s defined", 
				TAG_ALIAS);
			ret = -EINVAL;
			goto out;
		}
//		add_a_host(ctx, hostalias, resp);
		i = -ENOENT;
		hosts = json_object_get(ctx->root, TAG_HOSTS);
	        if (!hosts) {
		        hosts = json_array();
			json_object_set_new(ctx->root, TAG_HOSTS, hosts);
		}
		else 
			i = find_array(hosts, TAG_ALIAS, hostalias, &iter1);

		if(i == -ENOENT) {
			iter1 = json_object();
			json_set_string(iter1, TAG_ALIAS, hostalias);
			json_array_append_new(hosts, iter1);
		}
	}
		
	value = json_object_get(new, TAG_HOSTNQN);
	if (value) {
		strcpy(hostnqn, (char *) json_string_value(value));
		json_update_string(iter1, new, TAG_HOSTNQN, value);
		sprintf(resp, "%s '%s' updated ", TAG_HOSTS, hostalias);
	}
	
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

int show_host(void *context, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*hosts;
	json_t			*obj;
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

	sprintf(resp, "%s", json_dumps(obj, 0));
	return 0; 
}

/* TARGET */
/* SUBSYSTEMS */

int set_subsys(void *context, char *alias, char *ss, char *data,
	       int c_flag, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*subsyss;
	json_t			*obj;
	json_t			*iter1;
	json_t			*iter2;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	char			 ss_nqn[MAX_STRING + 1];
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
	
	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return-EINVAL;
	}
	value = json_object_get(new, TAG_SUBNQN);
	if (value)
		strcpy(ss_nqn, (char *)json_string_value(value));

	if (c_flag == MUST_EXIST) {
		json_get_array(obj, TAG_SUBSYSTEMS, subsyss);
		if (!subsyss)
			return -EINVAL;
		i = find_array(subsyss, TAG_SUBNQN, ss, &iter1);
		if (i < 0) {
			sprintf(resp, "%s '%s' not found in %s '%s'",
				TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
			return -ENOENT;
		}
		if (value && (strcmp(ss, ss_nqn) != 0)) {
			i = find_array(subsyss, TAG_SUBNQN, ss_nqn, &iter2);
			if (i != -ENOENT) {
                                sprintf(resp, "%s '%s' exists",
                                        TAG_SUBNQN, ss_nqn);
                                ret = -EEXIST;
                                goto out;
                        }
			json_update_string(iter1, new, TAG_SUBNQN, value);
		}

	}
	else {
		if (!value) {
                        sprintf(resp, "invalid json syntax, no %s defined",
                                TAG_SUBNQN);
                        ret = -EINVAL;
                        goto out;
                }
                i = -ENOENT;
		json_get_array(obj, TAG_SUBSYSTEMS, subsyss);
		if (!subsyss) {	
			subsyss = json_array();
			json_object_set_new(obj, TAG_SUBSYSTEMS, subsyss);
		} 
		else
			i = find_array(subsyss, TAG_SUBNQN, ss_nqn, &iter1);
		if (i == -ENOENT) {
			iter1 = json_object();
			json_set_string(iter1, TAG_SUBNQN, ss_nqn);
			json_array_append_new(subsyss, iter1);
		}
	}
	
//	if (!create) {
//		json_update_string(iter, new, TAG_SUBNQN, value);
//		rename_host_acl(ctx, ss, (char *) json_string_value(value));
//	}

	json_update_int(iter1, new, TAG_ALLOW_ANY, value);
	sprintf(resp, "%s '%s' updated in %s '%s'",
		TAG_SUBSYSTEM, ss, TAG_TARGET, alias);

out:
	json_decref(new);
	
	return ret;
}

int del_subsys(void *context, char *alias, char *ss, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*hosts;
	int			 ret;

	hosts = json_object_get(ctx->root, TAG_HOSTS);
	if (hosts)
		del_host_acl(hosts, ss);

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
		sprintf(resp, "%s not found",TAG_TARGETS);
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

	json_update_string(iter, new, TAG_DEVID, value);

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

int list_target(void *context, char *resp)
{
	char			*p = resp;
	int			 n;

	n = sprintf(p, "{");
	p += n;

	n = _list_target(context, p);
	p += n;

	sprintf(p, "}");

	return 0;
}

int  set_interface(void *context, char *target, char *data, char *resp)
{
	struct json_context     *ctx = context;
	json_t                  *targets;
        json_t                  *new;
        json_t                  *obj;
        json_t                  *iter;
        json_t                  *value;
        json_t                  *tmp;
        int                      i;
	json_error_t             error;

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

int add_to_targets(void *ct, char *target, char *data, char *resp, int c_flag)
{
	struct json_context	*ctx = ct;
	json_t			*targets;
	json_t			*iter;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	char			 newname[MAX_STRING + 1];
	char			 tgtalias[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;
	int			 added = 0;

	targets = json_object_get(ctx->root, TAG_TARGETS);
        if (!targets) {
                sprintf(resp, "%s '%s' not found", TAG_TARGETS, target);
                return -ENOENT;
        }

	new = json_loads(data, JSON_DECODE_ANY, &error);
	if (!new) {
		sprintf(resp, "invalid json syntax");
		return -EINVAL;
	}

	if(c_flag == 1) {
		value = json_object_get(new, TAG_ALIAS);
		if (value) {
			strcpy(tgtalias, (char *)json_string_value(value));
                        ret = add_a_target(ctx, tgtalias, resp);
                        if (ret != 0)
                            goto out;
		}
		else {
			sprintf(resp, "invalid json syntax, no %s defined", 
				TAG_ALIAS);
			ret = -EINVAL;
			goto out;
		}
	}
	else 
		strcpy(tgtalias, target);

//TBD Needs re-work

	value = json_object_get(new, TAG_REFRESH);
	if (value) {
		strcpy(tgtalias, (char *)json_string_value(value));
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

	sprintf(resp, "%s '%s' %s ", TAG_TARGET, newname,
		added ? "added" : "updated");
out:
	json_decref(new);

	return ret;
}

int add_a_target(void *context, char *alias, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*iter;
	json_t			*items;
	json_t			*tmp;
	int			 i;

	targets = json_object_get(ctx->root, TAG_TARGETS);
	if (!targets) {
		targets = json_array();
		json_object_set_new(ctx->root, TAG_TARGETS, targets);
	}
	else {
		i = find_array(targets, TAG_ALIAS, alias, &iter);
		if (i >= 0) {
			sprintf(resp, "%s '%s' exists", TAG_TARGET, alias);
			return -EEXIST;
		}
	}

	iter = json_object();
	json_set_string(iter, TAG_ALIAS, alias);
	
	items = json_array();
	json_object_set_new(iter, TAG_PORTIDS, items);
	
	items = json_array();
	json_object_set_new(iter, TAG_SUBSYSTEMS, items);
	
	json_array_append_new(targets, iter);
		

	sprintf(resp, "%s '%s' added", TAG_TARGET, alias);

	return 0;
}

int update_target(void *ctxt, char *target, char *data, char *resp, int c_flag)
{
	struct json_context	*ctx = ctxt;
	json_t			*targets;
	json_t			*iter1;
	json_t			*iter2;
	json_t			*items;
	json_t			*new;
	json_t			*value;
	json_t			*tmp;
	json_error_t		 error;
	char			 tgtalias[MAX_STRING + 1];
	int			 i;
	int			 ret = 0;

        new = json_loads(data, JSON_DECODE_ANY, &error);
        if (!new) {
                sprintf(resp, "invalid json syntax");
                return -EINVAL;
        }
	
	value = json_object_get(new, TAG_ALIAS);
        if (value) {
                strcpy(tgtalias, (char *)json_string_value(value));
        }

	if(c_flag == MUST_EXIST) {
		targets = json_object_get(ctx->root, TAG_TARGETS);
	        if (!targets) {
		        sprintf(resp, "%s '%s' not found", TAG_TARGETS, target);
			return -ENOENT;
		}
		i = find_array(targets, TAG_ALIAS, target, &iter1);
                if (i == -ENOENT) {
                        sprintf(resp, "%s '%s' does not exists",
                                TAG_TARGET, target);
                        return -ENOENT;
                }
                if(value && (strcmp(target, tgtalias) != 0)) {
                        i = find_array(targets, TAG_ALIAS, tgtalias, &iter2);
                        if (i != -ENOENT) {
                                sprintf(resp, "%s '%s' exists",
                                        TAG_TARGET, tgtalias);
                                ret = -EEXIST;
                                goto out;
                        }
                        json_update_string(iter1, new, TAG_ALIAS, value);
                }
		
	}
	else {
		if (!value) {
                        sprintf(resp, "invalid json syntax, no %s defined",
                                TAG_ALIAS);
                        ret = -EINVAL;
                        goto out;
                }
		i = -ENOENT;
		targets = json_object_get(ctx->root, TAG_TARGETS);
	        if (!targets) {
			targets = json_array();
			json_object_set_new(ctx->root, TAG_TARGETS, targets);
		}	
		else
			i = find_array(targets, TAG_ALIAS, tgtalias, &iter1);
		if(i == -ENOENT) {
			iter1 = json_object();
			json_set_string(iter1, TAG_ALIAS, tgtalias);
			
			items = json_array();
			json_object_set_new(iter1, TAG_PORTIDS, items);
	
			items = json_array();
			json_object_set_new(iter1, TAG_SUBSYSTEMS, items);
		
			json_array_append_new(targets, iter1);
		}
	}
	
	json_update_int(iter1, new, TAG_REFRESH, value);
	
	value = json_object_get(new, TAG_INTERFACE);
	json_object_set(iter1, TAG_INTERFACE, value);
	
	sprintf(resp, "%s '%s' updated ", TAG_TARGET, tgtalias);
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

int set_acl(void *context, char *alias, char *ss, char *nqn,
	    char *data, char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*hosts;
	json_t			*new;
	json_t			*subgroup;
	json_t			*obj;
	json_t			*iter;
	json_t			*value;
	json_t			*array;
	json_error_t		 error;
	json_t			*tmp;
	char			 host_nqn[MAX_STRING + 1];
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


	if(!nqn) {
		new = json_loads(data, JSON_DECODE_ANY, &error);
		if (!new) {
			sprintf(resp, "invalid json syntax");
	                return -EINVAL;
		}
		value = json_object_get(new, TAG_ALIAS);
		if (value) 
			strcpy(host_nqn, (char *)json_string_value(value));
	}		
	else 
		strcpy(host_nqn, nqn);

		
	i = find_array(hosts, TAG_HOSTNQN, host_nqn, NULL);
	if (i < 0) {
		sprintf(resp, "%s '%s' not found", TAG_HOSTNQN, nqn);
		return -ENOENT;
	}

	i = find_array(array, TAG_HOSTNQN, host_nqn, &iter);
	if (i >= 0) {
		sprintf(resp,
		"%s '%s' exists for %s '%s' in %s '%s'",
		TAG_HOST, nqn, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		return -EEXIST;
	}

	iter = json_object();
	json_set_string(iter, TAG_HOSTNQN, host_nqn);
	json_array_append_new(array, iter);

	sprintf(resp,
		"%s '%s' added for %s '%s' in %s '%s'",
		TAG_HOST, nqn, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);

	return 0;
}

int del_acl(void *context, char *alias, char *ss, char *nqn,
	    char *resp)
{
	struct json_context	*ctx = context;
	json_t			*targets;
	json_t			*subgroup;
	json_t			*array;
	int			 ret;
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

	ret = del_from_array(array, TAG_SUBSYSTEM, ss, TAG_HOSTS, nqn);
	if (ret) {
		sprintf(resp,
			"%s '%s' not found in %s '%s' for %s '%s'",
			TAG_HOST, nqn, TAG_SUBSYSTEM, ss, TAG_ALIAS, alias);
		return ret;
	}

	sprintf(resp,
		"%s '%s' deleted from %s '%s' for %s '%s'",
		TAG_HOST, nqn, TAG_SUBSYSTEM, ss, TAG_ALIAS, alias);

	return 0;
}

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
#include <unistd.h>
#include <errno.h>
#include <curl/curl.h>

#include "jansson.h"

#include "curl.h"
#include "tags.h"
#include "show.h"

enum { DEM = 0, TARGET, HOST, GROUP, END = -1 };
static char *groups[] = { URI_DEM, URI_TARGET, URI_HOST, URI_GROUP };

static char *dem_server = DEFAULT_ADDR;
static char *dem_port = DEFAULT_PORT;
static char *group = DEFAULT_GROUP;
static int prompt_deletes = 1;
int formatted;
int debug_curl;

enum { HUMAN = 0, RAW = -1, JSON = 1 };

#define JSSTR		"\"%s\":\"%s\""
#define JSINT		"\"%s\":%d"

#define _ADD		"add"
#define _SET		"set"
#define _DEL		"delete"
#define _RENA		"rename"
#define _GET		"get"
#define _LIST		"list"
#define _GROUP		"group"
#define _TARGET		"target"
#define _SUBSYS		"subsystem"
#define _HOST		"host"
#define _PORT		"portid"
#define _NSDEV		"drive"
#define _ACL		"acl"
#define _NS		"ns"
#define _INTERFACE	"interface"
#define _USAGE		"usage"

#define DELETE_PROMPT	"Are you sure you want to delete "

struct verbs {
	int		(*function)(void *ctx, char *base, int n, char **p);
	int		 target;
	int		 num_args;
	char		*verb;
	char		*object;
	char		*args;
	char		*help;
};

static char *error(int ret)
{
	if (ret == -EEXIST)
		return "already exists";

	if (ret == -ENOENT)
		return "not found";

	return "unknown error";
}

static inline int cancel_delete()
{
	char			 c;
	c = getchar();
	return (c != 'y' && c != 'Y');
}

/* DEM */

static int dem_config(void *ctx, char *base, int n, char **p)
{
	char			*result;
	json_t			*parent;
	json_error_t		 error;
	int			 ret;

	UNUSED(n);
	UNUSED(p);

	ret = exec_get(ctx, base, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_loads(result, JSON_DECODE_ANY, &error);
		if (parent)
			show_config(parent, formatted);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

static int dem_apply(void *ctx, char *base, int n, char **p)
{
	char			 url[128];

	UNUSED(n);
	UNUSED(p);

	snprintf(url, sizeof(url), "%s/%s", base, METHOD_APPLY);

	return exec_post(ctx, url, NULL, 0);
}

static int dem_shutdown(void *ctx, char *base, int n, char **p)
{
	char			 url[128];

	UNUSED(n);
	UNUSED(p);

	snprintf(url, sizeof(url), "%s/%s", base, METHOD_SHUTDOWN);

	return exec_post(ctx, url, NULL, 0);
}

/* GROUPS */

static int list_group(void *ctx, char *url, int n, char **p)
{
	int			 ret;
	char			*result;
	json_t			*parent;
	json_error_t		 error;

	UNUSED(n);
	UNUSED(p);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_loads(result, JSON_DECODE_ANY, &error);
		if (parent)
			show_group_list(parent, formatted);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

static int add_group(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_post(ctx, url, NULL, 0);
}

static int get_group(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;
	int			 ret;
	char			*result;
	json_t			*parent;
	json_error_t		 error;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_loads(result, JSON_DECODE_ANY, &error);
		if (parent)
			show_group_data(parent, formatted);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

static int set_group(void *ctx, char *url, int n, char **p)
{
	char			 data[256];
	char			*group = *p++;
	int			 len;

	UNUSED(n);

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_NAME, group);

	return exec_put(ctx, url, data, len);
}

static int del_group(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*group = *p;

	UNUSED(n);

	if (prompt_deletes) {
		printf(DELETE_PROMPT "%s '%s'? (N/y) ", TAG_GROUP, group);
		if (cancel_delete())
			return 0;
	}

	snprintf(url, sizeof(url), "%s/%s", base, group);

	return exec_delete(ctx, url);
}

static int rename_group(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*old = *p++;
	char			*new = *p;
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, old);

	len = snprintf(data, sizeof(data), "{" JSSTR" }", TAG_NAME, new);

	return exec_patch(ctx, url, data, len);
}

/* TARGETS */

static int list_target(void *ctx, char *url, int n, char **p)
{
	int			 ret;
	char			*result;
	json_t			*parent;
	json_error_t		 error;

	UNUSED(n);
	UNUSED(p);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_loads(result, JSON_DECODE_ANY, &error);
		if (parent)
			show_target_list(parent, formatted, 0);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

static int add_target(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_post(ctx, url, NULL, 0);
}

static int get_target(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;
	int			 ret;
	char			*result;
	json_t			*parent;
	json_error_t		 error;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_loads(result, JSON_DECODE_ANY, &error);
		if (parent)
			show_target_data(parent, formatted);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

static int set_target(void *ctx, char *url, int n, char **p)
{
	char			 data[256];
	char			*alias = *p++;
	int			 refresh = atoi(*p);
	int			 len;

	UNUSED(n);

	len = snprintf(data, sizeof(data), "{" JSSTR "," JSINT "}",
		       TAG_ALIAS, alias, TAG_REFRESH, refresh);


	return exec_put(ctx, url, data, len);
}

static int del_target(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;

	UNUSED(n);

	if (prompt_deletes) {
		printf(DELETE_PROMPT "%s '%s'? (N/y) ", TAG_TARGET, alias);
		if (cancel_delete())
			return 0;
	}

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_delete(ctx, url);
}

static int rename_target(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*old = *p++;
	char			*new = *p;
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, old);

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_ALIAS, new);

	return exec_patch(ctx, url, data, len);
}

static int refresh_target(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, METHOD_REFRESH);

	return exec_post(ctx, url, NULL, 0);
}

static int usage_target(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*result;
	char			*alias = *p;
	int			 ret;
	json_t			*parent;
	json_error_t		 error;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, METHOD_USAGE);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_loads(result, JSON_DECODE_ANY, &error);
		if (parent)
			show_usage_data(parent, formatted);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

/* NSDEVS */

static int set_nsdev(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[128];
	char			*alias = *p++;
	char			*nsdev = *p;
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, URI_NSDEV);

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_NSDEV, nsdev);

	return exec_put(ctx, url, data, len);
}

static int del_nsdev(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*alias = *p;
	int			 i;
	int			 len;

	if (prompt_deletes) {
		printf(DELETE_PROMPT "these %s from %s '%s'? (N/y) ",
		       TAG_NSDEVS, TAG_TARGET, alias);
		if (cancel_delete())
			return 0;
	}

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, URI_NSDEV);

	for (i = 0; i < n; i++) {
		len = snprintf(data, sizeof(data), "{" JSSTR "}",
			       TAG_NSDEV, *++p);
		exec_delete_ex(ctx, url, data, len);
	}

	return 0;
}

/* SUBSYSTEMS */

static int add_subsys(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;
	char			*nqn = *p++;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s/%s", base, alias, URI_SUBSYSTEM,
		 nqn);

	return exec_post(ctx, url, NULL, 0);
}

static int set_subsys(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*alias = *p++;
	char			*nqn = *p++;
	int			 allow_all = atoi(*p);
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s/%s", base, alias, URI_SUBSYSTEM,			 nqn);

	len = snprintf(data, sizeof(data), "{" JSINT "}",
		       TAG_ALLOW_ALL, allow_all);

	return exec_put(ctx, url, data, len);
}

static int del_subsys(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;
	int			 i;

	if (prompt_deletes) {
		printf(DELETE_PROMPT "these %s from %s '%s'? (N/y) ",
		       TAG_SUBSYSTEMS, TAG_TARGET, alias);
		if (cancel_delete())
			return 0;
	}

	for (i = 0; i < n; i++) {
		snprintf(url, sizeof(url), "%s/%s/%s/%s", base, alias,
			 URI_SUBSYSTEM, *++p);
		exec_delete(ctx, url);
	}

	return 0;
}

static int rename_subsys(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*alias = *p++;
	char			*old = *p++;
	char			*new = *p;
	int			 len;

	UNUSED(n);
	UNUSED(p);

	snprintf(url, sizeof(url), "%s/%s/%s/%s", base, alias, URI_SUBSYSTEM,
		 old);

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_SUBNQN, new);

	return exec_patch(ctx, url, data, len);
}

/* PORTID */

static int set_portid(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*alias = *p++;
	int			 portid = atoi(*p++);
	char			*type = *p++;
	char			*family = *p++;
	char			*address = *p++;
	int			 trsvcid = atoi(*p);
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, URI_PORTID);

	len = snprintf(data, sizeof(data),
		     "{" JSINT "," JSSTR "," JSSTR "," JSSTR "," JSINT "}",
		     TAG_PORTID, portid, TAG_TYPE, type, TAG_FAMILY, family,
		     TAG_ADDRESS, address, TAG_TRSVCID, trsvcid);

	return exec_put(ctx, url, data, len);
}

static int del_portid(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;
	int			 i;

	if (prompt_deletes) {
		printf(DELETE_PROMPT "these %s from %s '%s'? (N/y) ",
		       TAG_PORTIDS, TAG_TARGET, alias);
		if (cancel_delete())
			return 0;
	}

	for (i = 0; i < n; i++) {
		snprintf(url, sizeof(url), "%s/%s/%s/%s", base, alias,
			 URI_PORTID, *++p);
		exec_delete(ctx, url);
	}

	return 0;
}

/* ACL */

static int set_acl(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;
	char			*ss = *p++;
	char			*nqn = *p++;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s/%s/%s/%s", base, alias,
		 URI_SUBSYSTEM, ss, URI_HOST, nqn);

	return exec_put(ctx, url, NULL, 0);
}

static int del_acl(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;
	char			*ss = *p;
	int			 i;

	if (prompt_deletes) {
		printf(DELETE_PROMPT "these %s from %s '%s' on %s '%s'? (N/y) ",
		       TAG_HOSTS, TAG_SUBSYSTEM, ss, TAG_TARGET, alias);
		if (cancel_delete())
			return 0;
	}

	for (i = 0; i < n - 1; i++) {
		snprintf(url, sizeof(url), "%s/%s/%s/%s/%s/%s", base, alias,
			 URI_SUBSYSTEM, ss, URI_HOST, *++p);
		exec_delete(ctx, url);
	}

	return 0;
}

/* NAMESPACES */

static int set_ns(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*alias = *p++;
	char			*subsys = *p++;
	int			 nsid = atoi(*p++);
	char			*nsdev = *p;
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s/%s/%s", base, alias,
		 URI_SUBSYSTEM, subsys, URI_NAMESPACE);

	len = snprintf(data, sizeof(data), "{" JSINT "," JSSTR "}",
		       TAG_NSID, nsid, TAG_NSDEV, nsdev);

	return exec_put(ctx, url, data, len);
}

static int del_ns(void *ctx, char *base, int n, char **p)
{
	char			 base_url[128];
	char			 url[128];
	char			*alias = *p++;
	char			*subsys = *p;
	int			 i;

	if (prompt_deletes) {
		printf(DELETE_PROMPT
		       "these %s from %s '%s' on %s '%s'? (N/y) ",
		       TAG_NSIDS, TAG_SUBSYSTEM, subsys,
		       TAG_TARGET, alias);
		if (cancel_delete())
			return 0;
	}

	snprintf(base_url, sizeof(base_url), "%s/%s/%s/%s/%s",
		base, alias, URI_SUBSYSTEM, subsys, URI_NAMESPACE);
	n--;

	for (i = 0; i < n; i++) {
		p++;
		snprintf(url, sizeof(url), "%s/%d", base_url, atoi(*p));
		exec_delete_ex(ctx, url, NULL, 0);
	}

	return 0;
}

/* HOSTS */

static int list_host(void *ctx, char *url, int n, char **p)
{
	int			 ret;
	char			*result;
	json_t			*parent;
	json_error_t		 error;

	UNUSED(n);
	UNUSED(p);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_loads(result, JSON_DECODE_ANY, &error);
		if (parent)
			show_host_list(parent, formatted, 0);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

static int add_host(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_post(ctx, url, NULL, 0);
}


static int get_host(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;
	int			 ret;
	char			*result;
	json_t			*parent;
	json_error_t		 error;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_loads(result, JSON_DECODE_ANY, &error);
		if (parent)
			show_host_data(parent, formatted);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

static int set_host(void *ctx, char *url, int n, char **p)
{
	char			 data[256];
	char			*nqn = *p++;
	int			 len;

	UNUSED(n);

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_HOSTNQN, nqn);

	return exec_put(ctx, url, data, len);
}

static int del_host(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*nqn = *p;

	UNUSED(n);

	if (prompt_deletes) {
		printf(DELETE_PROMPT "%s '%s'? (N/y) ", TAG_HOST, nqn);
		if (cancel_delete())
			return 0;
	}

	snprintf(url, sizeof(url), "%s/%s", base, nqn);

	return exec_delete(ctx, url);
}

static int rename_host(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*old = *p++;
	char			*new = *p;
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, old);

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_HOSTNQN, new);

	return exec_patch(ctx, url, data, len);
}

/* INTERFACES */

static int set_interface(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*nqn = *p++;
	char			*type = *p++;
	char			*family = *p++;
	char			*address = *p++;
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, nqn, URI_INTERFACE);

	len = snprintf(data, sizeof(data), "{" JSSTR "," JSSTR "," JSSTR "}",
		     TAG_TYPE, type, TAG_FAMILY, family, TAG_ADDRESS, address);

	return exec_put(ctx, url, data, len);
}

static int del_interface(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*nqn = *p++;
	char			*type = *p++;
	char			*family = *p++;
	char			*address = *p++;
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, nqn, URI_INTERFACE);

	len = snprintf(data, sizeof(data), "{" JSSTR "," JSSTR "," JSSTR "}",
		       TAG_TYPE, type, TAG_FAMILY, family,
		       TAG_ADDRESS, address);

	return exec_delete_ex(ctx, url, data, len);
}

/*
 * Function	- Called when command given
 * Target	- Entity to be accesed
 * Num Args	- If <0 varibale # args allowed and indicates minimum args
 * Verb		- Action to be performed
 * Object	- Sub-entity action addresses
 * Args		- Can be NULL, fixed, or variable based on "Num Args"
 * Help		- Message describing what this verb does to this object
*/
static struct verbs verb_list[] = {
	/* DEM */
	{ dem_config,	 DEM,     0, "config",   NULL, NULL,
	  "show dem configuration including interfaces" },
	{ dem_apply,	 DEM,     0, "apply",    NULL, NULL,
	  "signal the dem to apply changes and rebuild"
	  " internal data structures" },
	{ dem_shutdown,	 DEM,     0, "shutdown", NULL, NULL,
	  "signal the dem to shutdown" },

	/* GROUPS */
	{ list_group,	 GROUP,   0, _LIST, _GROUP,  NULL,
	  "list all groups" },
	{ add_group,	 GROUP,   1, _ADD,  _GROUP, "<name>",
	  "create a group (using POST)" },
	{ get_group,	 GROUP,   1, _GET,  _GROUP, "<name>",
	  "show the targets and hosts associated with a specific group" },
	{ set_group,	 GROUP,   1, _SET,  _GROUP, "<name>",
	  "create/update a group (using PUT)" },
	{ del_group,	 GROUP,   1, _DEL,  _GROUP, "<name>",
	  "delete a specific group and all targets/hosts associated"
	  " with that group" },
	{ rename_group,	 GROUP,   2, _RENA, _GROUP, "<old> <new>",
	  "rename a specific group (using PATCH)" },

	/* TARGETS */
	{ list_target,	 TARGET,  0, _LIST, _TARGET,  NULL,
	  "list the targets of the group specified by the -g option" },
	{ add_target,	 TARGET,  1, _ADD,  _TARGET, "<alias>",
	  "create a target in the specified group (using POST)" },
	{ get_target,	 TARGET,  1, _GET,  _TARGET, "<alias>",
	  "show the specified target including refresh rate, ports,"
	  " ss's, and ns's" },
	{ set_target,	 TARGET,  2, _SET,  _TARGET, "<alias> <refresh(mins)>",
	  "create/update a target in the specified group (using PUT)" },
	{ del_target,	 TARGET,  1, _DEL,  _TARGET, "<alias>",
	  "delete a target in the specified group" },
	{ rename_target, TARGET,  2, _RENA, _TARGET, "<old> <new>",
	  "rename a target in the specified group (using PATCH)" },
	{ refresh_target, TARGET, 1, "refresh", _TARGET, "<alias>",
	  "signal the dem to refresh the log pages of a target/group" },
	{ usage_target, TARGET, 1, "usage", _TARGET, "<alias>",
	  "get usage for subsystems of a target/group" },

	/* NSDEVS */
	{ set_nsdev,	 TARGET,  2, _SET,  _NSDEV, "<alias> <nsdev>",
	  "create/update a physical namespace device to a target/group"
	  " (using PUT)" },
	{ del_nsdev,	 TARGET, -2, _DEL,  _NSDEV,
	  "<alias> <nsdev> {<nsdev> ...}",
	  "delete a set of physical ns devices from a target/group"
	  " (one at a time)" },

	/* SUBSYSTEMS */
	{ add_subsys,	 TARGET,  2, _ADD,  _SUBSYS, "<alias> <subnqn>",
	  "create a subsystem on a specific target/group (using POST)" },
	{ set_subsys,	 TARGET,  3, _SET,  _SUBSYS,
		"<alias> <subnqn> <allow_all>",
	  "create/update a subsystem on a specific target/group (using PUT)" },
	{ del_subsys,	 TARGET, -2, _DEL,  _SUBSYS,
		"<alias> <subnqn> {<subnqn> ...}",
	  "delete a set of subsystems from a target/group (one at a time)" },
	{ rename_subsys, TARGET,  3, _RENA, _SUBSYS,
		"<alias> <subnqn> <newnqn>",
	  "rename a subsustem on a specific target/group (using PATCH)" },

	/* PORTID */
	{ set_portid,	 TARGET,  6, _SET,  _PORT,
	  "<alias> <portid> <trtype> <adrfam> <traddr> <trsrvid>",
	  "create a port on a specific target/group (using PUT)" },
	{ del_portid,	 TARGET, -2, _DEL,  _PORT,
	  "<alias> <portid> {<portid> ...}",
	  "delete a set of ports from a target/group (one at a time)" },

	/* ACL */
	{ set_acl,	 TARGET,  3, _SET,  _ACL, "<alias> <subnqn> <hostnqn>",
	  "create/update a hosts access to a subsystem/target/group"
	  " (using PUT)" },
	{ del_acl,	 TARGET, -3, _DEL,  _ACL,
	  "<alias> <subnqn> <hostnqn> {<hostnqn> ...}",
	  "delete a set of hosts from access to a subsystem/target/group" },

	/* NAMESPACES */
	{ set_ns,	 TARGET,  4, _SET,  _NS,
	  "<alias> <subnqn> <nsid> <nsdev>",
	  "create/update a namespace on a specific subsystem/target/group"
	  " (using PUT)" },
	{ del_ns,	 TARGET, -3, _DEL,  _NS,
	  "<alias> <subnqn> <nsid> {<nsid> ...}",
	  "delete a set of namespaces from a subsystem/target/group"
	  " (one at a time)" },

	/* HOSTS */
	{ list_host,	 HOST,  0, _LIST, _HOST,  NULL,
	  "list the hosts of the group specified by the -g option" },
	{ add_host,	 HOST,  1, _ADD,  _HOST, "<hostnqn>",
	  "create a host in the specified group (using POST)" },
	{ get_host,	 HOST,  1, _GET,  _HOST, "<hostnqn>",
	  "show the specified host including interfaces" },
	{ set_host,	 HOST,  1, _SET,  _HOST, "<hostnqn>",
	  "create/update a host in the specified group (using PUT)" },
	{ del_host,	 HOST,  1, _DEL,  _HOST, "<hostnqn>",
	  "delete a host in the specified group" },
	{ rename_host,	 HOST,  2, _RENA, _HOST, "<hostnqn> <newnqn>",
	  "rename a host in the specified group (using PATCH)" },

	/* INTERFACES */
	{ set_interface, HOST,  4, _SET,  _INTERFACE,
	  "<hostnqn> <trtype> <adrfam> <traddr>",
	  "create/update an interface on a specific host/group (using PUT)" },
	{ del_interface, HOST, -3, _DEL,  _INTERFACE,
	  "<hostnqn> <trtype> <adrfam> <traddr>",
	  "delete an interface from a specific host/group" },
	{ NULL,		 END,   0,  NULL,  NULL,   NULL, "" },
};

/* utility functions */

static void show_help(char *prog, char *msg, char *opt)
{
	struct verbs		*p;
	int			target = END;

	if (msg) {
		if (opt)
			printf("Error: %s '%s'\n", msg, opt);
		else
			printf("Error: %s\n", msg);
		printf("use -? or --help to show full help\n");
		return;
	}

	printf("Usage: %s {options} <verb> {object} {value ...}\n", prog);
	printf("options: %s\n",
	       "{-f} {-c} {-g <group>} {-j|-r} {-s <server>} {-p <port>}");
	printf("  -f -- force - do not verify deletes\n");
	printf("  -c -- enable debug of curl commands being sent\n");
	printf("  -g -- group in which object exists (default %s)\n",
	       DEFAULT_GROUP);
	printf("  -j -- show output in json mode (less human readable)\n");
	printf("  -r -- show output in raw mode (unformatted)\n");
	printf("  -s -- specify server (default %s)\n", DEFAULT_ADDR);
	printf("  -p -- specify port (default %s)\n", DEFAULT_PORT);
	printf("\n");
	printf("  verb : list | get | add | set | rename | delete\n");
	printf("	 | refresh | config | apply | shutdown\n");
	printf("       : shorthand verbs may be used (first 3 characters)\n");
	printf("object : group | target | drive | subsystem | portid | acl\n");
	printf("	 | ns | host | interface\n");
	printf("       : shorthand objects may be used (first character)\n");
	printf("allow_all -- 0 : use host list\n");
	printf("	  -- 1 : allow all hosts access to a subsystem\n");
	printf("\n");

	for (p = verb_list; p->verb; p++) {
		if (target != p->target) {
			target = p->target;
			printf("%s commands:\n", groups[target]);
		}
		printf("  dem %s", p->verb);
		if (p->object)
			printf(" %s", p->object);
		if (p->args)
			printf(" %s", p->args);
		printf("\n");
		if (p->help)
			printf("    - %s\n", p->help);
	}
}

static struct verbs *find_verb(char *verb, char *object)
{
	struct verbs		*p;
	int			 verb_len = strlen(verb);
	int			 object_len = (object) ? strlen(object) : 0;

	for (p = verb_list; p->verb; p++) {
		if (strcmp(verb, p->verb)) {
			if (verb_len == 3 && strncmp(verb, p->verb, 3) == 0)
				; // short version of verb matches
			else
				continue;
		}
		if (!p->object)
			return p;
		if (!object)
			break;
		if (strcmp(object, p->object) == 0)
			return p;
		if (object_len == 1 && object[0] == p->object[0])
			return p; // short version of object matches
	}

	return NULL;
}

static int valid_arguments(char *prog, int argc, int target, int expect)
{
	const int		 base = (target == DEM) ? 1 : 2;
	int			 ret = 0;

	if (expect < 0) {
		expect = -expect; // get absolute value
		if (argc < base  + expect) {
			show_help(prog, "missing attrs", NULL);
			ret = -1;
		}
	} else if (argc < base + expect) {
		show_help(prog, "missing attrs", NULL);
		ret = -1;
	} else if (argc > base + expect) {
		show_help(prog, "extra attrs", NULL);
		ret = -1;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct verbs		*p;
	char			**args;
	char			**opts;
	int			 n;
	int			 opt;
	int			 ret = -1;
	char			 url[128];
	void			*ctx;

	if (argc <= 1 || strcmp(argv[1], "--help") == 0) {
		show_help(argv[0], NULL, NULL);
		return 0;
	}

	formatted = HUMAN;
	debug_curl = 0;

	while ((opt = getopt(argc, argv, "?jcrfg:s:p:")) != -1) {
		switch (opt) {
		case 'c':
			debug_curl = 1;
			break;
		case 'r':
			formatted = RAW;
			break;
		case 'j':
			formatted = JSON;
			break;
		case 'f':
			prompt_deletes = 0;
			break;
		case 'g':
			group = optarg;
			break;
		case 's':
			dem_server = optarg;
			break;
		case 'p':
			dem_port = optarg;
			break;
		case '?':
			show_help(argv[0], NULL, NULL);
			return 0;
		default:
			show_help(argv[0], "Unknown option", NULL);
			return 1;
		}
	}

	args = &argv[optind];
	argc -= optind;

	p = find_verb(args[0], (argc <= 1) ? NULL : args[1]);
	if (!p) {
		show_help(argv[0], "unknown verb/object set", NULL);
		return -1;
	}

	if (valid_arguments(argv[0], argc, p->target, p->num_args))
		return -1;

	ctx = init_curl();
	if (!ctx)
		return -1;

	if (p->target == DEM)
		snprintf(url, sizeof(url), "http://%s:%s/%s",
			 dem_server, dem_port, groups[DEM]);
	else if (p->target == GROUP)
		snprintf(url, sizeof(url), "http://%s:%s/%s",
			 dem_server, dem_port, URI_GROUP);
	else
		snprintf(url, sizeof(url), "http://%s:%s/%s/%s/%s",
			 dem_server, dem_port, URI_GROUP, group,
			 groups[p->target]);

	if (argc <= 1)
		opts = args;
	else {
		argc -= 3;
		opts = &args[2];
	}

	ret = p->function(ctx, url, argc, opts);
	if (ret < 0) {
		n = (strcmp(p->verb, _RENA) == 0 && ret == -EEXIST) ? 4 : 3;
		printf("Error: %s: %s '%s' %s\n",
		       argv[0], args[1], args[n], error(ret));
	}

	cleanup_curl(ctx);

	return ret;
}

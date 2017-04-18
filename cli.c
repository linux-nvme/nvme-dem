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
#include <unistd.h>
#include <errno.h>
#include <curl/curl.h>
#include <json-c/json.h>

#include "curl.h"
#include "tags.h"

enum { CTRL = 0, HOST, DEM, END = -1 };
static char *group[] = { TARGET_CTRL, TARGET_HOST, TARGET_DEM };

char *dem_server = "127.0.0.1";
char *dem_port = "22345";
int prompt_deletes = 1;
int formatted;

enum { HUMAN = 0, RAW = -1, JSON = 1 };

struct verbs {
	int (*function)(void *ctx, char *base, int n, char **p);
	int target;
	int num_args;
	char *verb;
	char *object;
	char *args;
};

char *error(int ret)
{
	if (ret == -EEXIST)
		return "already exists";

	if (ret == -ENOENT)
		return "not found";

	return "unknown error";
}

static int list_ctrl(void *ctx, char *url, int n, char **p)
{
	int ret;
	char *result;
	struct json_object *parent;

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_tokener_parse(result);
		if (parent)
			show_ctrl_list(parent, formatted);
		else
			printf("%s\n", result);
	}
	free(result);

	return 0;
}

static int show_ctrl(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;
	int ret;
	char *result;
	struct json_object *parent;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_tokener_parse(result);
		if (parent)
			show_ctrl_data(parent, formatted);
		else
			printf("%s\n", result);
	}
	free(result);

	return 0;
}

static int list_host(void *ctx, char *url, int n, char **p)
{
	int ret;
	char *result;
	struct json_object *parent;

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_tokener_parse(result);
		if (parent)
			show_host_list(parent, formatted);
		else
			printf("%s\n", result);
	}

	free(result);

	return 0;
}

static int show_host(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;
	int ret;
	char *result;
	struct json_object *parent;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	if (formatted == RAW)
		printf("%s\n", result);
	else {
		parent = json_tokener_parse(result);
		if (parent)
			show_host_data(parent, formatted);
		else
			printf("%s\n", result);
	}
	free(result);

	return 0;
}

static int del_entry(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_delete(ctx, url);
}

static int rename_entry(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *old = *p++;
	char *new = *p;

	snprintf(url, sizeof(url), "%s/%s", base, old);

	return exec_post(ctx, url, new, strlen(new));
}

static int set_host(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char data[256];
	char *alias = *p++;
	char *nqn = *p++;
	int access = atoi(*p);
	int len;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	len = snprintf(data, sizeof(data),
			"{ \"%s\" : \"%s\", \"%s\" : %d }",
			TAG_NQN, nqn, TAG_ACCESS, access);

	return exec_put(ctx, url, data, len);
}

static int set_ctrl(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char data[256];
	char *alias = *p++;
	char *type = *p++;
	char *family = *p++;
	char *address = *p++;
	int port = atoi(*p++);
	int refresh = atoi(*p);
	int len;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	len = snprintf(data, sizeof(data),
			"{ \"%s\" : \"%s\", \"%s\" : \"%s\", "
			"\"%s\" : \"%s\", \"%s\" : %d, \"%s\" : %d }",
			TAG_TYPE, type, TAG_FAMILY, family,
			TAG_ADDRESS, address, TAG_PORT, port,
			TAG_REFRESH, refresh);

	return exec_put(ctx, url, data, len);
}

static int set_subsys(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char data[256];
	char *alias = *p++;
	char *nqn = *p++;
	int allow_all = atoi(*p);
	int len;

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, nqn);

	len = snprintf(data, sizeof(data), "{ \"%s\" : %d }",
		       TAG_ALLOW_ALL, allow_all);

	return exec_put(ctx, url, data, len);
}

static int set_acl(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char data[256];
	char *alias = *p++;
	char *nqn = *p++;
	int access = atoi(*p);
	int len;

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, nqn);

	len = snprintf(data, sizeof(data), "{ \"%s\" : %d }",
		       TAG_ACCESS, access);

	return exec_put(ctx, url, data, len);
}

static int refresh_ctrl(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, METHOD_REFRESH);

	return exec_post(ctx, url, NULL, 0);
}

static int del_array(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;
	int i;

	for (i = 0; i < n; i++) {
		snprintf(url, sizeof(url), "%s/%s/%s", base, alias, *++p);
		exec_delete(ctx, url);
	}

	return 0;
}

static int rename_array(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p++;
	char *old = *p++;
	char *new = *p;

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, old);

	return exec_post(ctx, url, new, strlen(new));
}

static int dem_shutdown(void *ctx, char *base, int n, char **p)
{
	return exec_post(ctx, base, METHOD_SHUTDOWN, sizeof(METHOD_SHUTDOWN));
}

static int dem_config(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;
	char *result;
	int ret;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	ret = exec_get(ctx, url, &result);
	if (ret)
		return ret;

	printf("%s\n", result);

	free(result);

	return 0;
}

/*
 * Function	- Called when command given
 * Target	- Entity to be accesed
 * Num Args	- If <0 varibale # args allowed and indicates minimum args
 * Verb		- Action to be performed
 * Object	- Sub-entity action addresses
 * Args		- Can be NULL, fixed, or variable based on "Num Args"
*/
struct verbs verb_list[] = {
	{ list_ctrl,	CTRL,  0, "list",    "ctrl",  NULL },
	{ set_ctrl,	CTRL,  6, "set",     "ctrl",
		"<alias> <trtype> <addrfam> <traddr> <trport> <refresh>" },
	{ show_ctrl,	CTRL,  1, "show",    "ctrl", "<alias>" },
	{ del_entry,	CTRL,  1, "delete",  "ctrl", "<alias>" },
	{ rename_entry,	CTRL,  2, "rename",  "ctrl", "<old> <new>" },
	{ refresh_ctrl,	CTRL,  1, "refresh", "ctrl", "<alias>" },
	{ set_subsys,	CTRL,  3, "set",     "ss",
		"<alias> <nqn> <allow_all>" },
	{ del_array,	CTRL, -2, "delete",  "ss",   "<alias> <nqn> ..." },
	{ rename_array,	CTRL,  3, "rename",  "ss",   "<alias> <old> <new>" },
	{ list_host,	HOST,  0, "list",    "host",  NULL },
	{ set_host,	HOST,  1, "set",     "host", "<nqn>" },
	{ show_host,	HOST,  1, "show",    "host", "<nqn>" },
	{ del_entry,	HOST,  1, "delete",  "host", "<nqn>" },
	{ rename_entry,	HOST,  2, "rename",  "host", "<old> <new>" },
	{ set_acl,	HOST,  3, "set",     "acl",
		"<host_nqn> <ss_nqn> <access>" },
	{ del_array,	HOST, -2, "delete",  "acl",
		"<host_nqn> <ss_nqn> ..." },
	{ dem_shutdown,	DEM,   0, "shutdown", NULL,   NULL },
	{ dem_config,	DEM,   0, "config", NULL,   NULL },
	{ NULL,		END,   0,  NULL,  NULL,   NULL },
};

/* utility functions */

static void show_help(char *prog, char *msg, char *opt)
{
	struct verbs *p;

	if (msg) {
		if (opt)
			printf("Error: %s '%s'\n", msg, opt);
		else
			printf("Error: %s\n", msg);
	}

	printf("Usage: %s {options} <verb> <object> {value ...}\n", prog);
	printf("obtions: {-f} {-s <server>} {-p <port>}");
	printf("  -f -- force - do not verify deletes\n");
	printf("  -s -- specify server (default %s)\n", dem_server);
	printf("  -p -- specify port (default %s)\n", dem_port);
	printf("allow_all -- 0 : use acl -- 1 allow all hosts access to ss\n");
	printf("access -- 0 : no access -- 1 : read only -- 2"
	       " : write only -- 3 : read/write\n");
	printf("  verb : list | set | show | rename | delete");
	printf(" | refresh | config | shutdown\n");
	printf("       : shorthand verbs may be use (first 3 characters)\n");
	printf("object : ctrl | ss | host | acl\n");
	printf("       : shorthand objects may be used (first character)\n");
	printf("commands:\n");

	for (p = verb_list; p->verb; p++) {
		printf("  dem %s", p->verb);
		if (p->object)
			printf(" %s", p->object);
		if (p->args)
			printf(" %s", p->args);
		printf("\n");
	}
}

static struct verbs *find_verb(char *verb, char *object)
{
	struct verbs *p;
	int verb_len = strlen(verb);
	int object_len = (object) ? strlen(object) : 0;

	for (p = verb_list; p->verb; p++) {
		if (strcmp(verb, p->verb))
			if (verb_len == 3 && strncmp(verb, p->verb, 3) == 0)
				; // short version of verb matches
			else
				continue;
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
	const int base = (target == DEM) ? 1 : 2;
	int ret = 0;

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
	struct verbs *p;
	char **args;
	char **opts;
	int n;
	int opt;
	int ret = -1;
	char url[128];
	void *ctx;

	if (argc <= 1 || strcmp(argv[1], "--help") == 0) {
		show_help(argv[0], NULL, NULL);
		return 0;
	}

	formatted = HUMAN;

	while ((opt = getopt(argc, argv, "jrfp:s:")) != -1) {
		switch (opt) {
		case 'r':
			formatted = RAW;
			break;
		case 'j':
			formatted = JSON;
			break;
		case 'f':
			prompt_deletes = 0;
			break;
		case 'p':
			dem_port = optarg;
			break;
		case 's':
			dem_server = optarg;
			break;
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

	snprintf(url, sizeof(url), "http://%s:%s/%s", dem_server, dem_port,
		 group[p->target]);

	if (argc <= 1)
		opts = args;
	else {
		argc -= 3;
		opts = &args[2];
	}

	ret = p->function(ctx, url, argc, opts);
	if (ret < 0) {
		n = (strcmp(p->verb, "rename") == 0 && ret == -EEXIST) ? 4 : 3;
		printf("Error: %s: %s '%s' %s\n",
		       argv[0], args[1], args[n], error(ret));
	}

	cleanup_curl(ctx);

	return ret;
}

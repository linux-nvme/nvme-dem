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
#include <curl/curl.h>

#include "curl.h"

enum { CTRL = 0, HOST, DEM, END = -1 };
static char *group[] = { "controller", "host", "dem" };

struct verbs {
	int (*execute)(void *ctx, char *base, int n, char **p);
	int idx;
	int args;
	char *verb;
	char *object;
	char *arglst;
};

char *error(int ret)
{
	if (ret == -EEXIST)
		return "already exists";

	if (ret == -ENOENT)
		return "not found";

	return "unknown error";
}

static int list_group(void *ctx, char *url, int n, char **p)
{
	return exec_get(ctx, url);
}

static int del_entry(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_delete(ctx, url);
}

static int show_entry(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_get(ctx, url);
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
	char *alias = *p;

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_put(ctx, url, NULL, 0);
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

	len = snprintf(data, sizeof(data),
			"{ \"Type\" : \"%s\", \"Family\" : \"%s\", "
			"\"Address\" : \"%s\", \"Port\" : %d, "
			"\"Refresh\" : %d }",
			type, family, address, port, refresh);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_put(ctx, url, data, len);
}

static int refresh_ctrl(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;

	snprintf(url, sizeof(url), "%s/%s/refresh", base, alias);

	return exec_post(ctx, url, NULL, 0);
}

static int add_array(void *ctx, char *base, int n, char **p)
{
	char url[128];
	char *alias = *p;
	int i;

	for (i = 0; i < n; i++) {
		snprintf(url, sizeof(url), "%s/%s/%s", base, alias, *++p);
		exec_put(ctx, url, NULL, 0);
	}

	return 0;
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
	return exec_post(ctx, base, "shutdown", 8);
}

struct verbs verb_list[] = {
	{ list_group,	CTRL,  0, "list",    "ctrl",  NULL },
	{ set_ctrl,	CTRL,  6, "set",     "ctrl",
		"<alias> <trtype> <addrfam> <traddr> <trport> <refresh>" },
	{ show_entry,	CTRL,  1, "show",    "ctrl", "<alias>" },
	{ del_entry,	CTRL,  1, "delete",  "ctrl", "<alias>" },
	{ rename_entry,	CTRL,  2, "rename",  "ctrl", "<old> <new>" },
	{ refresh_ctrl,	CTRL,  1, "refresh", "ctrl", "<alias>" },
	{ add_array,	CTRL, -2, "add",     "ss",   "<alias> <nqn> ..." },
	{ del_array,	CTRL, -2, "delete",  "ss",   "<alias> <nqn> ..." },
	{ rename_array,	CTRL,  3, "rename",  "ss", "<alias> <old> <new>" },
	{ list_group,	HOST,  0, "list",    "host",  NULL },
	{ set_host,	HOST,  1, "set",     "host", "<nqn>" },
	{ show_entry,	HOST,  1, "show",    "host", "<nqn>" },
	{ del_entry,	HOST,  1, "delete",  "host", "<nqn>" },
	{ rename_entry,	HOST,  2, "rename",  "host", "<old> <new>" },
	{ add_array,	HOST, -2, "add",     "acl", "<host_nqn> <ss_nqn> ..." },
	{ del_array,	HOST, -2, "delete",  "acl", "<host_nqn> <ss_nqn> ..." },
	{ dem_shutdown,	DEM,   0, "shutdown", NULL,   NULL },
	{ list_group,	DEM,   0, "config", NULL,   NULL },
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

	printf("Usage: %s <verb> <object> {value ...}\n", prog);
	printf("  verb : add | delete | set | list | show | rename | ");
	printf("refresh | config | shutdown\n");
	printf("       : shorthand verbs may be use (first 3 characters)\n");
	printf("object : ctrl | ss | host | acl\n");
	printf("       : shorthand objects may be used (first character)\n");

	for (p = verb_list; p->verb; p++) {
		printf("  %s", p->verb);
		if (p->object)
			printf(" %s", p->object);
		if (p->arglst)
			printf(" %s", p->arglst);
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

static int valid_arguments(char *prog, int argc, int idx, int expect)
{
	const int n = (idx == DEM) ? 2 : 3;
	int ret = 0;

	if (expect < 0) {
		if (argc < n - expect) {
			show_help(prog, "missing attrs", NULL);
			ret = -1;
		}
	} else if (argc < n + expect) {
		show_help(prog, "missing attrs", NULL);
		ret = -1;
	} else if (argc > n + expect) {
		show_help(prog, "extra attrs", NULL);
		ret = -1;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct verbs *p;
	int n;
	int ret = -1;
	char server[] = "127.0.0.1:12345";
	char url[128];
	void *ctx;

	if (argc <= 1 || strcmp(argv[1], "--help") == 0) {
		show_help(argv[0], NULL, NULL);
		return 0;
	}

	p = find_verb(argv[1], (argc == 2) ? NULL : argv[2]);
	if (!p) {
		show_help(argv[0], "unknown verb/object set", NULL);
		return -1;
	}

	if (valid_arguments(argv[0], argc, p->idx, p->args))
		return -1;

	snprintf(url, sizeof(url), "http://%s/%s", server, group[p->idx]);
	ctx = init_curl();
	if (!ctx)
		return -1;

	ret = p->execute(ctx, url, argc - 4, &argv[3]);
	if (ret < 0) {
		n = (strcmp(p->verb, "rename") == 0 && ret == -EEXIST) ? 4 : 3;
		printf("Error: %s: %s '%s' %s\n",
		       argv[0], argv[2], argv[n], error(ret));
	}

	cleanup_curl(ctx);

	return ret;
}

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

enum { DEM = 0, CTLR, HOST, GROUP, END = -1 };
static char *groups[] = { TARGET_DEM, TARGET_CTLR, TARGET_HOST, TARGET_GROUP };

static char *dem_server = DEFAULT_ADDR;
static char *dem_port = DEFAULT_PORT;
static char *group = DEFAULT_GROUP;
static int prompt_deletes = 1;
int formatted;

enum { HUMAN = 0, RAW = -1, JSON = 1 };

#define JSSTR		"\"%s\":\"%s\""
#define JSINT		"\"%s\":%d"

struct verbs {
	int		(*function)(void *ctx, char *base, int n, char **p);
	int		 target;
	int		 num_args;
	char		*verb;
	char		*object;
	char		*args;
};

static char *error(int ret)
{
	if (ret == -EEXIST)
		return "already exists";

	if (ret == -ENOENT)
		return "not found";

	return "unknown error";
}

static int list_ctlr(void *ctx, char *url, int n, char **p)
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
			show_ctlr_list(parent, formatted, 0);
		else
			printf("%s\n", result);
	}
	free(result);

	return 0;
}

static int get_ctlr(void *ctx, char *base, int n, char **p)
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
			show_ctlr_data(parent, formatted);
		else
			printf("%s\n", result);
	}
	free(result);

	return 0;
}

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

static int del_entry(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;
	char			 c;

	UNUSED(n);

	if (prompt_deletes) {
		printf("Are you sure you want to delete %s '%s'? (N/y) ",
			base, alias);
		c = getchar();
		if (c != 'y' && c != 'Y')
			return 0;
	}

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_delete(ctx, url);
}

static int add_host(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_post(ctx, url, NULL, 0);
}

static int set_host(void *ctx, char *url, int n, char **p)
{
	char			 data[256];
	char			*nqn = *p++;
	char			*cert = *p++;
	char			*unique_nqn = *p++;
	int			 len;

	UNUSED(n);

	len = snprintf(data, sizeof(data), "{" JSSTR "," JSSTR "," JSSTR "}",
		       TAG_NQN, nqn, TAG_CERT, cert, TAG_ALIAS_NQN, unique_nqn);

	return exec_put(ctx, url, data, len);
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

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_NQN, new);

	return exec_patch(ctx, url, data, len);
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

static int set_group(void *ctx, char *url, int n, char **p)
{
	char			 data[256];
	char			*group = *p++;
	int			 len;

	UNUSED(n);

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_NAME, group);

	return exec_put(ctx, url, data, len);
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

static int set_ctlr(void *ctx, char *url, int n, char **p)
{
	char			 data[256];
	char			*alias = *p++;
	char			*cert = *p++;
	char			*type = *p++;
	char			*family = *p++;
	char			*address = *p++;
	int			 port = atoi(*p++);
	int			 refresh = atoi(*p);
	int			 i, len = 0, sz = sizeof(data);
	char			*d = data;

	UNUSED(n);

	i = snprintf(d, sz, "{" JSSTR "," JSSTR "," JSINT ",\"%s\":{",
		     TAG_ALIAS, alias, TAG_CERT, cert, TAG_REFRESH, refresh,
		     TAG_TRANSPORT);
	len += i; sz -= i; d += i;

	i = snprintf(d, sz, JSSTR "," JSSTR "," JSSTR "," JSINT "}}",
		     TAG_TYPE, type, TAG_FAMILY, family,
		     TAG_ADDRESS, address, TAG_PORT, port);
	len += i;

	return exec_put(ctx, url, data, len);
}

static int add_ctlr(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s", base, alias);

	return exec_post(ctx, url, NULL, 0);
}

static int rename_ctlr(void *ctx, char *base, int n, char **p)
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

static int set_subsys(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*alias = *p++;
	char			*nqn = *p++;
	int			 allow_all = atoi(*p);
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, nqn);

	len = snprintf(data, sizeof(data), "{" JSINT "}",
		       TAG_ALLOW_ALL, allow_all);

	return exec_put(ctx, url, data, len);
}

static int add_subsys(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p++;
	char			*nqn = *p++;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, nqn);

	return exec_post(ctx, url, NULL, 0);
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

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, old);

	len = snprintf(data, sizeof(data), "{" JSSTR "}", TAG_NQN, new);

	return exec_patch(ctx, url, data, len);
}

static int set_acl(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			 data[256];
	char			*alias = *p++;
	char			*nqn = *p++;
	int			 access = atoi(*p);
	int			 len;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, nqn);

	len = snprintf(data, sizeof(data), "{" JSINT "}", TAG_ACCESS, access);

	return exec_put(ctx, url, data, len);
}

static int refresh_ctlr(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;

	UNUSED(n);

	snprintf(url, sizeof(url), "%s/%s/%s", base, alias, METHOD_REFRESH);

	return exec_post(ctx, url, NULL, 0);
}

static int del_array(void *ctx, char *base, int n, char **p)
{
	char			 url[128];
	char			*alias = *p;
	char			 c;
	int			 i;

	if (prompt_deletes) {
		printf("Are you sure you want to delete %s '%s'? (N/y) ",
			base, alias);
		c = getchar();
		if (c != 'y' && c != 'Y')
			return 0;
	}

	for (i = 0; i < n; i++) {
		snprintf(url, sizeof(url), "%s/%s/%s", base, alias, *++p);
		exec_delete(ctx, url);
	}

	return 0;
}

static int dem_shutdown(void *ctx, char *base, int n, char **p)
{
	char			 url[128];

	UNUSED(n);
	UNUSED(p);

	snprintf(url, sizeof(url), "%s/%s", base, METHOD_SHUTDOWN);

	return exec_post(ctx, url, NULL, 0);
}

static int dem_apply(void *ctx, char *base, int n, char **p)
{
	char			 url[128];

	UNUSED(n);
	UNUSED(p);

	snprintf(url, sizeof(url), "%s/%s", base, METHOD_APPLY);

	return exec_post(ctx, url, NULL, 0);
}

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

/*
 * Function	- Called when command given
 * Target	- Entity to be accesed
 * Num Args	- If <0 varibale # args allowed and indicates minimum args
 * Verb		- Action to be performed
 * Object	- Sub-entity action addresses
 * Args		- Can be NULL, fixed, or variable based on "Num Args"
*/
static struct verbs verb_list[] = {
	{ dem_config,	 DEM,   0, "config",   NULL, NULL },
	{ dem_apply,	 DEM,   0, "apply",    NULL, NULL },
	{ dem_shutdown,	 DEM,   0, "shutdown", NULL, NULL },
	/* GROUPS */
	{ list_group,	 GROUP, 0, "list",    "group",  NULL },
	{ get_group,	 GROUP, 1, "get",     "group", "<name>" },
	{ add_group,	 GROUP, 1, "add",     "group", "<name>" },
	{ set_group,	 GROUP, 1, "set",     "group", "<name>" },
	{ del_entry,	 GROUP, 1, "delete",  "group", "<name>" },
	{ rename_group,	 GROUP, 2, "rename",  "group", "<old> <new>" },
	/* CONTROLLERS */
	{ list_ctlr,	 CTLR,  0, "list",    "ctlr",  NULL },
	{ get_ctlr,	 CTLR,  1, "get",     "ctlr", "<alias>" },
	{ add_ctlr,	 CTLR,  1, "add",     "ctlr", "<alias>" },
	{ set_ctlr,	 CTLR,  7, "set",     "ctlr",
		"<alias> <cert> <trtype> <addrfam> <addr> "
		"<port> <refresh (mins)>" },
	{ del_entry,	 CTLR,  1, "delete",  "ctlr", "<alias>" },
	{ rename_ctlr,	 CTLR,  2, "rename",  "ctlr", "<old> <new>" },
	{ refresh_ctlr,	 CTLR,  1, "refresh", "ctlr", "<alias>" },
	/* SUBSYSTEMS */
	{ add_subsys,	 CTLR,  2, "add",     "ss", "<alias> <nqn>" },
	{ set_subsys,	 CTLR,  3, "set",     "ss",
		"<alias> <nqn> <allow_all>" },
	{ del_array,	 CTLR, -2, "delete",  "ss", "<alias> <nqn> ..." },
	{ rename_subsys, CTLR , 3, "rename",  "ss", "<alias> <old> <new>" },
	/* HOSTS */
	{ list_host,	 HOST,  0, "list",    "host",  NULL },
	{ get_host,	 HOST,  1, "get",     "host", "<nqn>" },
	{ add_host,	 HOST,  1, "add",     "host", "<nqn>" },
	{ set_host,	 HOST,  3, "set",     "host",
		"<nqn> <cert> <unique_nqn>" },
	{ del_entry,	 HOST,  1, "delete",  "host", "<nqn>" },
	{ rename_host,	 HOST,  2, "rename",  "host", "<old> <new>" },
	/* ACL */
	{ set_acl,	 HOST,  3, "set",     "acl",
		"<host_nqn> <ss_nqn> <access>" },
	{ del_array,	 HOST, -2, "delete",  "acl",
		"<host_nqn> <ss_nqn> ..." },
	{ NULL,		 END,   0,  NULL,  NULL,   NULL },
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
	       "{-f} {-g <group>} {-j|-r} {-s <server>} {-p <port>}");
	printf("  -f -- force - do not verify deletes\n");
	printf("  -g -- group in which object exists (default %s)\n",
	       DEFAULT_GROUP);
	printf("  -j -- show output in json mode (less human readable)\n");
	printf("  -r -- show output in raw mode (unformatted)\n");
	printf("  -s -- specify server (default %s)\n", DEFAULT_ADDR);
	printf("  -p -- specify port (default %s)\n", DEFAULT_PORT);
	printf("\n");
	printf("  verb : list | get | add | set | rename | delete\n");
	printf("         | refresh | config | apply | shutdown\n");
	printf("       : shorthand verbs may be used (first 3 characters)\n");
	printf("object : ctlr | ss | host | acl\n");
	printf("       : shorthand objects may be used (first character)\n");
	printf("allow_all -- 0 : use acl\n");
	printf("          -- 1 : allow all hosts access to ss\n");
	printf("   access -- 0 : no access\n");
	printf("          -- 1 : read only\n");
	printf("          -- 2 : write only\n");
	printf("          -- 3 : read/write\n");
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

	while ((opt = getopt(argc, argv, "?jrfg:s:p:")) != -1) {
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
			 dem_server, dem_port, TARGET_GROUP);
	else
		snprintf(url, sizeof(url), "http://%s:%s/%s/%s/%s",
			 dem_server, dem_port, TARGET_GROUP, group,
			 groups[p->target]);

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

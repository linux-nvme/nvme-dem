/*
 * NVMe over Fabrics Distributed Endpoint Manager (NVMe-oF DEM).
 * Copyright (c) 2017-2018 Intel Corporation, Inc. All rights reserved.
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

int add_group(char *group, char *resp)
{
	int			 ret;

	ret = add_json_group(group, resp);

	return ret;
}

int update_group(char *group, char *data, char *resp)
{
	int			 ret;

	ret = update_json_group(group, data, resp);

	return ret;
}

int set_group_member(char *group, char *data, char *tag, char *parent_tag,
		     char *resp)
{
	int			 ret;

	ret = set_json_group_member(group, data, tag, parent_tag, resp);

	return ret;
}

int del_group_member(char *group, char *member, char *tag, char *parent_tag,
		     char *resp)
{
	int			 ret;

	ret = del_json_group_member(group, member, tag, parent_tag, resp);

	return ret;
}

int del_group(char *group, char *resp)
{
	int			 ret;

	ret = del_json_group(group, resp);

	return ret;
}

int add_host(char *host, char *resp)
{
	int			 ret;

	ret = add_json_group(host, resp);

	return ret;
}

int update_host(char *host, char *data, char *resp)
{
	int			 ret;

	ret = update_json_host(host, data, resp);

	return ret;
}

int del_host(char *host, char *resp)
{
	int			 ret;

	ret = del_json_host(host, resp);

	return ret;
}

int set_subsys(char *alias, char *ss, char *data, char *resp)
{
	int			 ret;

	ret = set_json_subsys(alias, ss, data, resp);

	return ret;
}

int del_subsys(char *alias, char *ss, char *resp)
{
	int			 ret;

	ret = del_json_subsys(alias, ss, resp);

	return ret;
}

/* DRIVE */

int set_drive(char *alias, char *data, char *resp)
{
	int			 ret;

	ret = set_json_drive(alias, data, resp);

	return ret;
}

int del_drive(char *alias, char *data, char *resp)
{
	int			 ret;

	ret = del_json_drive(alias, data, resp);

	return ret;
}

/* PORTID */

int set_portid(char *target, int portid, char *data, char *resp)
{
	int			 ret;

	ret = set_json_portid(target, portid, data, resp);

	return ret;
}

int del_portid(char *alias, int portid, char *resp)
{
	int			 ret;

	ret = del_json_portid(alias, portid, resp);

	return ret;
}

/* NAMESPACE */

int set_ns(char *alias, char *ss, char *data, char *resp)
{
	int			 ret;

	ret = set_json_ns(alias, ss, data, resp);

	return ret;
}

int del_ns(char *alias, char *ss, int ns, char *resp)
{
	int			 ret;

	ret = del_json_ns(alias, ss, ns, resp);

	return ret;
}

/* TARGET */

int del_target(char *alias, char *resp)
{
	int			 ret;

	ret = del_json_target(alias, resp);

	return ret;
}

int set_interface(char *target, char *data, char *resp)
{
	int			 ret;

	ret = set_json_interface(target, data, resp);

	return ret;
}

int add_target(char *alias, char *resp)
{
	int			 ret;

	ret = add_json_target(alias, resp);

	return ret;
}

int update_target(char *target, char *data, char *resp)
{
	int			 ret;

	ret = update_json_target(target, data, resp);

	return ret;
}

int set_acl(char *alias, char *ss, char *host_uri, char *data, char *resp)
{
	int			 ret;

	ret = set_json_acl(alias, ss, host_uri, data, resp);

	return ret;
}

int del_acl(char *alias, char *ss, char *host, char *resp)
{
	int			 ret;

	ret = del_json_acl(alias, ss, host, resp);

	return ret;
}

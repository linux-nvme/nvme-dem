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

#define TAG_CTRLS		"Controllers"
#define TAG_HOSTS		"Hosts"
#define TAG_ALIAS		"Alias"
#define TAG_REFRESH		"Refresh"
#define TAG_TRANSPORT		"Transport"
#define TAG_TYPE		"Type"
#define TAG_FAMILY		"Family"
#define TAG_ADDRESS		"Address"
#define TAG_PORT		"Port"
#define TAG_SUBSYSTEMS		"Subsystems"
#define TAG_NQN			"NQN"
#define TAG_ALLOW_ALL		"AllowAllHosts"
#define TAG_ACL			"ACL"
#define TAG_ACCESS		"Access"
#define TAG_INTERFACES		"Inferfaces"
#define TAG_ID			"ID"

#define TARGET_CTRL		"controller"
#define TARGET_HOST		"host"
#define TARGET_DEM		"dem"

#define CTRL_LEN		(sizeof(TARGET_CTRL) - 1)
#define HOST_LEN		(sizeof(TARGET_HOST) - 1)
#define DEM_LEN			(sizeof(TARGET_DEM) - 1)

#define METHOD_SHUTDOWN		"shutdown"
#define METHOD_APPLY		"apply"
#define METHOD_REFRESH		"refresh"

#define ACCESS_NONE		0
#define ACCESS_READ		1
#define ACCESS_WRITE		2
#define ACCESS_RW		3

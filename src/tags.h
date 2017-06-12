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

#define TAG_GROUPS		"Groups"
#define TAG_GROUP		"Group"
#define TAG_CTLRS		"Controllers"
#define TAG_CTLR		"Controller"
#define TAG_HOSTS		"Hosts"
#define TAG_HOST		"Host"
#define TAG_ALIAS		"Alias"
#define TAG_REFRESH		"Refresh"
#define TAG_TRANSPORT		"Transport"
#define TAG_TYPE		"Type"
#define TAG_FAMILY		"Family"
#define TAG_ADDRESS		"Address"
#define TAG_PORT		"Port"
#define TAG_SUBSYSTEMS		"Subsystems"
#define TAG_SUBSYSTEM		"Subsystem"
#define TAG_NQN			"NQN"
#define TAG_ALLOW_ALL		"AllowAllHosts"
#define TAG_ACL			"ACL"
#define TAG_ACCESS		"Access"
#define TAG_INTERFACES		"Inferfaces"
#define TAG_ID			"ID"
#define TAG_CERT		"Certificate"
#define TAG_ALIAS_NQN		"DomainUniqueNQN"
#define TAG_NAME		"Name"
#define TAG_DRIVES		"Drives"
#define TAG_DRIVE		"Drive"

#define TARGET_GROUP		"group"
#define TARGET_CTLR		"ctlr"
#define TARGET_HOST		"host"
#define TARGET_DEM		"dem"

#define GROUP_LEN		(sizeof(TARGET_GROUP) - 1)
#define CTLR_LEN		(sizeof(TARGET_CTLR) - 1)
#define HOST_LEN		(sizeof(TARGET_HOST) - 1)
#define DEM_LEN			(sizeof(TARGET_DEM) - 1)

#define METHOD_SHUTDOWN		"shutdown"
#define METHOD_APPLY		"apply"
#define METHOD_REFRESH		"refresh"

#define DEFAULT_ADDR		"127.0.0.1"
#define DEFAULT_PORT		"22345"
#define DEFAULT_GROUP		"local"

enum {
	ACCESS_NONE = 0,
	ACCESS_READ,
	ACCESS_WRITE,
	ACCESS_RW,
};

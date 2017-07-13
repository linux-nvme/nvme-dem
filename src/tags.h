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
#define TAG_TARGETS		"Targets"
#define TAG_TARGET		"Target"
#define TAG_HOSTS		"Hosts"
#define TAG_HOST		"Host"
#define TAG_ALIAS		"Alias"
#define TAG_REFRESH		"Refresh"
#define TAG_TRANSPORT		"Transport"
#define TAG_TYPE		"TRTYPE"
#define TAG_FAMILY		"ADRFAM"
#define TAG_ADDRESS		"TRADDR"
#define TAG_TRSVCID		"TRSVCID"
#define TAG_TREQ		"TREQ"
#define TAG_PORTID		"PORTID"
#define TAG_PORTIDS		"PortIDs"
#define TAG_SUBSYSTEMS		"Subsystems"
#define TAG_SUBSYSTEM		"Subsystem"
#define TAG_SUBNQN		"SUBNQN"
#define TAG_HOSTNQN		"HOSTNQN"
#define TAG_ALLOW_ALL		"AllowAllHosts"
#define TAG_ACL			"ACL"
#define TAG_INTERFACES		"Inferfaces"
#define TAG_INTERFACE		"Inferface"
#define TAG_NAME		"Name"
#define TAG_NSDEVS		"NSDevs"
#define TAG_NSDEV		"NSDEV"
#define TAG_NSID		"NSID"
#define TAG_NSIDS		"NSIDs"
#define TAG_NAMESPACE		"Namespace"

/* DEM config specific */
#define TAG_ID			"ID"
#define TAG_PORT		"PORT"

#define URI_GROUP		"group"
#define URI_TARGET		"target"
#define URI_HOST		"host"
#define URI_DEM			"dem"
#define URI_NSDEV		"nsdev"
#define URI_SUBSYSTEM		"subsys"
#define URI_PORTID		"portid"
#define URI_NAMESPACE		"namespace"
#define URI_INTERFACE		"interface"

#define GROUP_LEN		(sizeof(URI_GROUP) - 1)
#define TARGET_LEN		(sizeof(URI_TARGET) - 1)
#define HOST_LEN		(sizeof(URI_HOST) - 1)
#define DEM_LEN			(sizeof(URI_DEM) - 1)

#define METHOD_SHUTDOWN		"shutdown"
#define METHOD_APPLY		"apply"
#define METHOD_REFRESH		"refresh"

#define DEFAULT_ADDR		"127.0.0.1"
#define DEFAULT_PORT		"22345"
#define DEFAULT_GROUP		"local"

/* SPDX-License-Identifier: DUAL GPL-2.0/BSD */
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

#ifndef __TAGS_H__
#define __TAGS_H__

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
#define TAG_ALLOW_ANY		"AllowAnyHost"
#define TAG_INTERFACES		"Interfaces"
#define TAG_INTERFACE		"Interface"
#define TAG_NAME		"Name"
#define TAG_DEVID		"DeviceID"
#define TAG_DEVNSID		"DeviceNSID"
#define TAG_NSID		"NSID"
#define TAG_NSIDS		"NSIDs"
#define TAG_NAMESPACE		"Namespace"
#define TAG_IFFAMILY		"FAMILY"
#define TAG_IFADDRESS		"ADDRESS"
#define TAG_IFPORT		"PORT"
#define TAG_SHARED		"Shared"
#define TAG_RESTRICTED		"Restricted"
#define TAG_MGMT_MODE		"MgmtMode"
#define TAG_OUT_OF_BAND_MGMT	"OutOfBandMgmt"
#define TAG_LOCAL_MGMT		"LocalMgmt"
#define TAG_IN_BAND_MGMT	"InBandMgmt"

/* Endport Config specific */
#define TAG_NSDEVS		"NSDevices"
#define URI_NSDEV		"nsdev"
#define URI_NAMESPACE		"namespace"
#define URI_CONFIG		"config"

/* JSON config specific */
#define TAG_ID			"ID"
#define TAG_PORT		"PORT"
#define TAG_NEW			"NEW"
#define TAG_OLD			"OLD"

#define URI_GROUP		"group"
#define URI_TARGET		"target"
#define URI_HOST		"host"
#define URI_DEM			"dem"
#define URI_SUBSYSTEM		"subsystem"
#define URI_PORTID		"portid"
#define URI_NSID		"nsid"
#define URI_INTERFACE		"interface"
#define URI_TRANSPORT		"transport"
#define URI_SIGNATURE		"signature"
#define URI_LOG_PAGE		"logpage"
#define URI_USAGE		"usage"
#define URI_PARM_MODE		"mode="
#define URI_PARM_FABRIC		"fabric="

#define GROUP_LEN		(sizeof(URI_GROUP) - 1)
#define TARGET_LEN		(sizeof(URI_TARGET) - 1)
#define HOST_LEN		(sizeof(URI_HOST) - 1)
#define DEM_LEN			(sizeof(URI_DEM) - 1)
#define PARM_MODE_LEN		(sizeof(URI_PARM_MODE) - 1)
#define PARM_FABRIC_LEN		(sizeof(URI_PARM_FABRIC) - 1)

#define METHOD_SHUTDOWN		"shutdown"
#define METHOD_REFRESH		"refresh"
#define METHOD_RECONFIG		"reconfig"

#define DEFAULT_HTTP_ADDR	"127.0.0.1"
#define DEFAULT_HTTP_PORT	"22345"

#define TRTYPE_STR_RDMA		"rdma"
#define TRTYPE_STR_FC		"fc"
#define TRTYPE_STR_TCP		"tcp"

#define ADRFAM_STR_IPV4		"ipv4"
#define ADRFAM_STR_IPV6		"ipv6"
#define ADRFAM_STR_FC		"fc"

#define NULL_BLK_DEVID		-1
#define INVALID_DEVID		-2

#endif

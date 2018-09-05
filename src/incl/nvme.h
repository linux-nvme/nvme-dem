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

#ifndef __NVME_H__
#define __NVME_H__

#include <linux/types.h>

#define NVMF_NQN_FIELD_LEN		256
#define NVMF_NQN_SIZE			223
#define NVMF_TRSVCID_SIZE		32
#define NVMF_TRADDR_SIZE		256
#define NVMF_TSAS_SIZE			256
#define NVMF_DEV_PATH_SIZE		256
#define NVME_DISC_SUBSYS_NAME		"nqn.2014-08.org.nvmexpress.discovery"
#define NVME_RDMA_IP_PORT		4420
#define NVME_CNTLID_DYNAMIC		0xFFFF

enum {
	nvme_admin_get_log_page		= 0x02,
	nvme_admin_identify		= 0x06,
	nvme_admin_set_features		= 0x09,
	nvme_admin_get_features		= 0x0A,
	nvme_admin_async_event		= 0x0C,
	nvme_admin_keep_alive		= 0x18,
	nvme_fabrics_command		= 0x7f,
};

enum {
	NVME_SC_SUCCESS			= 0x0,
	NVME_SC_INVALID_OPCODE		= 0x1,
	NVME_SC_INVALID_FIELD		= 0x2,
	NVME_SC_CMDID_CONFLICT		= 0x3,
	NVME_SC_DATA_XFER_ERROR		= 0x4,
	NVME_SC_POWER_LOSS		= 0x5,
	NVME_SC_INTERNAL		= 0x6,
	NVME_SC_ABORT_REQ		= 0x7,
	NVME_SC_ABORT_QUEUE		= 0x8,
	NVME_SC_FUSED_FAIL		= 0x9,
	NVME_SC_FUSED_MISSING		= 0xA,
	NVME_SC_INVALID_NS		= 0xB,
	NVME_SC_CMD_SEQ_ERROR		= 0xC,
	NVME_SC_SGL_INVALID_LAST	= 0xD,
	NVME_SC_SGL_INVALID_COUNT	= 0xE,
	NVME_SC_SGL_INVALID_DATA	= 0xF,
	NVME_SC_SGL_INVALID_METADATA	= 0x10,
	NVME_SC_SGL_INVALID_TYPE	= 0x11,
	NVME_SC_SGL_INVALID_OFFSET	= 0x16,
	NVME_SC_SGL_INVALID_SUBTYPE	= 0x17,
	NVME_SC_LBA_RANGE		= 0x80,
	NVME_SC_CAP_EXCEEDED		= 0x81,
	NVME_SC_NS_NOT_READY		= 0x82,
	NVME_SC_RESERVATION_CONFLICT	= 0x83,
	NVME_SC_CQ_INVALID		= 0x100,
	NVME_SC_QID_INVALID		= 0x101,
	NVME_SC_QUEUE_SIZE		= 0x102,
	NVME_SC_ABORT_LIMIT		= 0x103,
	NVME_SC_ABORT_MISSING		= 0x104,
	NVME_SC_ASYNC_LIMIT		= 0x105,
	NVME_SC_FIRMWARE_SLOT		= 0x106,
	NVME_SC_FIRMWARE_IMAGE		= 0x107,
	NVME_SC_INVALID_VECTOR		= 0x108,
	NVME_SC_INVALID_LOG_PAGE	= 0x109,
	NVME_SC_INVALID_FORMAT		= 0x10A,
	NVME_SC_FW_NEEDS_CONV_RESET	= 0x10B,
	NVME_SC_INVALID_QUEUE		= 0x10C,
	NVME_SC_FEATURE_NOT_SAVEABLE	= 0x10D,
	NVME_SC_FEATURE_NOT_CHANGEABLE	= 0x10E,
	NVME_SC_FEATURE_NOT_PER_NS	= 0x10F,
	NVME_SC_FW_NEEDS_SUBSYS_RESET	= 0x110,
	NVME_SC_FW_NEEDS_RESET		= 0x111,
	NVME_SC_FW_NEEDS_MAX_TIME	= 0x112,
	NVME_SC_FW_ACIVATE_PROHIBITED	= 0x113,
	NVME_SC_OVERLAPPING_RANGE	= 0x114,
	NVME_SC_NS_INSUFFICENT_CAP	= 0x115,
	NVME_SC_NS_ID_UNAVAILABLE	= 0x116,
	NVME_SC_NS_ALREADY_ATTACHED	= 0x118,
	NVME_SC_NS_IS_PRIVATE		= 0x119,
	NVME_SC_NS_NOT_ATTACHED		= 0x11A,
	NVME_SC_THIN_PROV_NOT_SUPP	= 0x11B,
	NVME_SC_CTRL_LIST_INVALID	= 0x11C,
	NVME_SC_BAD_ATTRIBUTES		= 0x180,
	NVME_SC_INVALID_PI		= 0x181,
	NVME_SC_READ_ONLY		= 0x182,
	NVME_SC_ONCS_NOT_SUPPORTED	= 0x183,
	NVME_SC_CONNECT_FORMAT		= 0x180,
	NVME_SC_CONNECT_CTRL_BUSY	= 0x181,
	NVME_SC_CONNECT_INVALID_PARAM	= 0x182,
	NVME_SC_CONNECT_RESTART_DISC	= 0x183,
	NVME_SC_CONNECT_INVALID_HOST	= 0x184,
	NVME_SC_DISCOVERY_RESTART	= 0x190,
	NVME_SC_AUTH_REQUIRED		= 0x191,
	NVME_SC_WRITE_FAULT		= 0x280,
	NVME_SC_READ_ERROR		= 0x281,
	NVME_SC_GUARD_CHECK		= 0x282,
	NVME_SC_APPTAG_CHECK		= 0x283,
	NVME_SC_REFTAG_CHECK		= 0x284,
	NVME_SC_COMPARE_FAILED		= 0x285,
	NVME_SC_ACCESS_DENIED		= 0x286,
	NVME_SC_UNWRITTEN_BLOCK		= 0x287,
	NVME_SC_DNR			= 0x4000,
};

enum {
	NVME_ID_CNS_CTRL		= 1,
};

enum {
	NVME_REG_CAP			= 0,
	NVME_REG_VS			= 0x08,
	NVME_REG_CC			= 0x14,
	NVME_REG_CSTS			= 0x1C,
};

enum {
	NVME_FEAT_ASYNC_EVENT		= 0x0B,
	NVME_FEAT_KATO			= 0x0F,
	NVME_LOG_DISC			= 0x70,
};

enum {
	NVME_AER_NOTICE_LOG_PAGE_CHANGE = 0xF002,
};

enum {
	NVME_NQN_DISC			= 1,
	NVME_NQN_NVME			= 2,
};
enum {
	NVMF_ADDR_FAMILY_PCI		= 0,
	NVMF_ADDR_FAMILY_IP4		= 1,
	NVMF_ADDR_FAMILY_IP6		= 2,
	NVMF_ADDR_FAMILY_IB		= 3,
	NVMF_ADDR_FAMILY_FC		= 4,
};

enum {
	NVMF_TRTYPE_RDMA		= 1,
	NVMF_TRTYPE_FC			= 2,
	NVMF_TRTYPE_TCP			= 3,
	NVMF_TRTYPE_LOOP		= 254,
	NVMF_TRTYPE_MAX,
};

enum {
	NVMF_TREQ_NOT_SPECIFIED		= 0,
	NVMF_TREQ_REQUIRED		= 1,
	NVMF_TREQ_NOT_REQUIRED		= 2,
};

enum {
	NVMF_RDMA_QPTYPE_CONNECTED	= 1,
	NVMF_RDMA_QPTYPE_DATAGRAM	= 2,
};

enum {
	NVMF_RDMA_PRTYPE_NOT_SPECIFIED	= 1,
	NVMF_RDMA_PRTYPE_IB		= 2,
	NVMF_RDMA_PRTYPE_ROCE		= 3,
	NVMF_RDMA_PRTYPE_ROCEV2		= 4,
	NVMF_RDMA_PRTYPE_IWARP		= 5,
};

enum {
	NVMF_RDMA_CMS_RDMA_CM		= 1,
};

enum {
	NVME_SGL_FMT_DATA_DESC		= 0,
	NVME_SGL_FMT_SEG_DESC		= 2,
	NVME_SGL_FMT_LAST_SEG_DESC	= 3,
	NVME_KEY_SGL_FMT_DATA_DESC	= 4,
	NVME_TRANSPORT_SGL_DATA_DESC	= 5,
};

enum {
	NVME_CMD_SGL_METABUF		= 0x40,
};

enum {
	NVME_AEN_CFG_DISC_LOG_CHG	= 1 << 31,
};

struct nvme_sgl_desc {
	__le64			addr;
	__le32			length;
	__u8			rsvd[3];
	__u8			type;
};

struct nvme_keyed_sgl_desc {
	__le64			addr;
	__u8			length[3];
	__u8			key[4];
	__u8			type;
};

union nvme_data_ptr {
	struct {
		__le64		prp1;
		__le64		prp2;
	};
	struct nvme_sgl_desc	sgl;
	struct nvme_keyed_sgl_desc ksgl;
};

struct nvme_common_command {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__le32			cdw2[2];
	__le64			rsvd;
	union nvme_data_ptr	dptr;
	__le32			cdw10[6];
};


struct nvmf_disc_rsp_page_entry {
	__u8			trtype;
	__u8			adrfam;
	__u8			subtype;
	__u8			treq;
	__le16			portid;
	__le16			cntlid;
	__le16			rsvd1;
	__u8			rsvd2[22];
	char			trsvcid[NVMF_TRSVCID_SIZE];
	__u8			rsvd3[192];
	char			subnqn[NVMF_NQN_FIELD_LEN];
	char			traddr[NVMF_TRADDR_SIZE];
	union tsas {
		char		common[NVMF_TSAS_SIZE];
		struct rdma {
			__u8	qptype;
			__u8	prtype;
			__u8	cms;
			__u8	rsvd[5];
			__u16	pkey;
		} rdma;
	} tsas;
};

struct nvmf_disc_rsp_page_hdr {
	__le64			genctr;
	__le64			numrec;
	__le16			recfmt;
	__u8			rsvd[1006];
	struct nvmf_disc_rsp_page_entry entries[0];
};

struct nvme_id_power_state {
	__u8			rsvd[32];
};

struct nvme_id_ctrl {
	__le16			vid;
	__le16			ssvid;
	char			sn[20];
	char			mn[40];
	char			fr[8];
	__u8			rab;
	__u8			ieee[3];
	__u8			cmic;
	__u8			mdts;
	__le16			cntlid;
	__le32			ver;
	__le32			rtd3r;
	__le32			rtd3e;
	__le32			oaes;
	__le32			ctratt;
	__u8			rsvd100[156];
	__le16			oacs;
	__u8			acl;
	__u8			aerl;
	__u8			frmw;
	__u8			lpa;
	__u8			elpe;
	__u8			npss;
	__u8			avscc;
	__u8			apsta;
	__le16			wctemp;
	__le16			cctemp;
	__le16			mtfa;
	__le32			hmpre;
	__le32			hmmin;
	__u8			tnvmcap[16];
	__u8			unvmcap[16];
	__le32			rpmbs;
	__le16			edstt;
	__u8			dsto;
	__u8			fwug;
	__le16			kas;
	__le16			hctma;
	__le16			mntmt;
	__le16			mxtmt;
	__le32			sanicap;
	__le32			hmminds;
	__le16			hmmaxd;
	__u8			rsvd338[174];
	__u8			sqes;
	__u8			cqes;
	__le16			maxcmd;
	__le32			nn;
	__le16			oncs;
	__le16			fuses;
	__u8			fna;
	__u8			vwc;
	__le16			awun;
	__le16			awupf;
	__u8			nvscc;
	__u8			rsvd531;
	__le16			acwu;
	__u8			rsvd534[2];
	__le32			sgls;
	__u8			rsvd540[228];
	char			subnqn[256];
	__u8			rsvd1024[768];
	__le32			ioccsz;
	__le32			iorcsz;
	__le16			icdoff;
	__u8			ctrattr;
	__u8			msdbd;
	__u8			rsvd1804[244];
	struct nvme_id_power_state	psd[32];
	__u8			vs[1024];
};

struct nvmf_connect_command {
	__u8			opcode;
	__u8			rsvd1;
	__u16			command_id;
	__u8			fctype;
	__u8			rsvd2[19];
	union nvme_data_ptr	dptr;
	__le16			recfmt;
	__le16			qid;
	__le16			sqsize;
	__u8			cattr;
	__u8			rsvd3;
	__le32			kato;
	__u8			rsvd4[12];
};

struct nvmf_connect_data {
	char			hostid[16];
	__le16			cntlid;
	char			rsvd4[238];
	char			subsysnqn[NVMF_NQN_FIELD_LEN];
	char			hostnqn[NVMF_NQN_FIELD_LEN];
	char			rsvd5[256];
};

struct nvmf_property_set_command {
	__u8			opcode;
	__u8			rsvd1;
	__u16			command_id;
	__u8			fctype;
	__u8			rsvd2[35];
	__u8			attrib;
	__u8			rsvd3[3];
	__le32			offset;
	__le64			value;
	__u8			rsvd4[8];
};

struct nvmf_property_get_command {
	__u8			opcode;
	__u8			rsvd1;
	__u16			command_id;
	__u8			fctype;
	__u8			rsvd2[35];
	__u8			attrib;
	__u8			rsvd3[3];
	__le32			offset;
	__u8			rsvd4[16];
};

struct nvme_identify {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2[2];
	union nvme_data_ptr	dptr;
	__u8			cns;
	__u8			rsvd3;
	__le16			ctrlid;
	__u32			rsvd11[5];
};

#define NVME_IDENTIFY_DATA_SIZE 4096

struct nvme_features {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2[2];
	union nvme_data_ptr	dptr;
	__le32			fid;
	__le32			dword11;
	__le32			dword12;
	__le32			dword13;
	__le32			dword14;
	__le32			dword15;
};

struct nvme_get_log_page_command {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2[2];
	union nvme_data_ptr	dptr;
	__u8			lid;
	__u8			rsvd10;
	__le16			numdl;
	__le16			numdu;
	__u16			rsvd11;
	__le32			lpol;
	__le32			lpou;
	__u32			rsvd14[2];
};

enum nvmf_capsule_command {
	nvme_fabrics_type_property_set		= 0x00,
	nvme_fabrics_type_connect		= 0x01,
	nvme_fabrics_type_property_get		= 0x04,
	nvme_fabrics_type_resource_config_reset	= 0x08,
	nvme_fabrics_type_resource_config_set	= 0x09,
	nvme_fabrics_type_resource_config_get	= 0x0A,
};

struct nvmf_common_command {
	__u8			opcode;
	__u8			rsvd1;
	__u16			command_id;
	__u8			fctype;
	__u8			rsvd2[35];
	__u8			ts[24];
};

enum {
	nvmf_get_ns_config	= 0x01,
	nvmf_get_xport_config	= 0x02,
	nvmf_reset_config	= 0x03,
	nvmf_set_port_config	= 0x04,
	nvmf_del_port_config	= 0x05,
	nvmf_link_port_config	= 0x06,
	nvmf_unlink_port_config	= 0x07,
	nvmf_set_subsys_config	= 0x08,
	nvmf_del_subsys_config	= 0x09,
	nvmf_set_ns_config	= 0x0a,
	nvmf_del_ns_config	= 0x0b,
	nvmf_set_host_config	= 0x0c,
	nvmf_del_host_config	= 0x0d,
	nvmf_link_host_config	= 0x0e,
	nvmf_unlink_host_config	= 0x0f,
};

struct nvmf_resource_config_command {
	__u8			opcode;
	__u8			rsvd1;
	__u16			command_id;
	__u8			fctype;
	__u8			rsvd2[59];
};

struct nvmf_port_config_entry {
	__u8			trtype;
	__u8			adrfam;
	__u8			rsvd;	/* subtype */
	__u8			treq;
	__le16			portid;
	char			trsvcid[NVMF_TRSVCID_SIZE];
	char			traddr[NVMF_TRADDR_SIZE];
};

struct nvmf_subsys_config_entry {
	char			subnqn[NVMF_NQN_FIELD_LEN];
	__u8			allowanyhost;
};

struct nvmf_ns_config_entry {
	char			subnqn[NVMF_NQN_FIELD_LEN];
	__le32			nsid;
	__le32			deviceid;
	__le32			devicensid;
};

struct nvmf_host_config_entry {
	char			hostnqn[NVMF_NQN_FIELD_LEN];
};

struct nvmf_subsys_delete_entry {
	char			subnqn[NVMF_NQN_FIELD_LEN];
};

struct nvmf_ns_delete_entry {
	char			subnqn[NVMF_NQN_FIELD_LEN];
	__le32			nsid;
};

struct nvmf_host_delete_entry {
	char			hostnqn[NVMF_NQN_FIELD_LEN];
};

struct nvmf_port_delete_entry {
	__le16			portid;
};

struct nvmf_link_host_entry {
	char			subnqn[NVMF_NQN_FIELD_LEN];
	char			hostnqn[NVMF_NQN_FIELD_LEN];
};

struct nvmf_link_port_entry {
	char			subnqn[NVMF_NQN_FIELD_LEN];
	__le16			portid;
};

struct nvmf_get_transports_entry {
	__u8			trtype;
	__u8			adrfam;
	__u8			rsvd[6];
	char			traddr[NVMF_TRADDR_SIZE];
};

struct nvmf_get_transports_hdr {
	__u8			num_entries;
	__u8			data;	/* Reference to first entry */
};

#define NVMF_NULLB_DEVID	255

struct nvmf_get_ns_devices_entry {
	__u8			devid;
	__u8			nsid;
	__u8			rsvd[6];
};

struct nvmf_get_ns_devices_hdr {
	__u8			num_entries;
	__u8			data;	/* Reference to first entry */
};

struct nvme_command {
	union {
		struct nvme_common_command		common;
		struct nvme_identify			identify;
		struct nvme_features			features;
		struct nvme_get_log_page_command	get_log_page;
		struct nvmf_common_command		fabrics;
		struct nvmf_connect_command		connect;
		struct nvmf_property_set_command	prop_set;
		struct nvmf_property_get_command	prop_get;
		struct nvmf_resource_config_command	config;
	};
};

struct nvme_completion {
	union nvme_result {
		__le16		U16;
		__le32		U32;
		__le64		U64;
	} result;
	__le16			sq_head;
	__le16			sq_id;
	__u16			command_id;
	__le16			status;
};

enum nvme_rdma_cm_fmt {
	NVME_RDMA_CM_FMT_1_0	= 0x0,
};


struct nvme_rdma_cm_req {
	__le16			recfmt;
	__le16			qid;
	__le16			hrqsize;
	__le16			hsqsize;
	__u8			rsvd[24];
};

struct nvme_rdma_cm_rep {
	__le16			recfmt;
	__le16			crqsize;
	__u8			rsvd[28];
};

#endif

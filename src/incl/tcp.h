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
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef __NVME_TCP_H__
#define __NVME_TCP_H__

enum nvme_tcp_connect_fmt {
	NVME_TCP_CONNECT_FMT_1_0 = 0x0,
};

enum nvme_tcp_pdu_type {
	NVME_TCP_ICREQ		= 0x0,
	NVME_TCP_ICRESP		= 0x1,
	NVME_TCP_H2CTERMREQ	= 0x2,
	NVME_TCP_C2HTERMREQ	= 0x3,
	NVME_TCP_CAPSULECMD	= 0x4,
	NVME_TCP_CAPSULERESP	= 0x5,
	NVME_TCP_H2CDATA	= 0x7,
	NVME_TCP_C2HDATA	= 0x8,
	NVME_TCP_R2T		= 0x9,
};

enum nvme_tcp_pdu_formatversion {
	NVME_TCP_PDU_FORMAT_VER	= 0x0,
};

enum {
	NVME_TCP_SINGLE_INFLIGHT_READY_TO_XMIT = 1,
};

struct nvme_tcp_common_hdr {
	__u8			pdu_type;
	__u8                    flags;
	__u8			hlen;
	__u8			pdo;
	__le32			plen;
};

struct nvme_tcp_icreq_pdu {
	struct nvme_tcp_common_hdr c_hdr;
	__le16			pfv;
	__u8			hpda;
	__u8			dgst;
	__le32			maxr2t;
	__u8			rsvd[112];
};

struct nvme_tcp_icresp_pdu {
	struct nvme_tcp_common_hdr c_hdr;
	__le16			pfv;
	__u8			cpda;
	__u8			dgst;
	__le32			maxh2c;
	__u8			rsvd[112];
};

struct nvme_tcp_cmd_capsule_pdu {
	struct nvme_tcp_common_hdr c_hdr;
	struct nvme_command	cmd;
};

struct nvme_tcp_resp_capsule_pdu {
	struct nvme_tcp_common_hdr c_hdr;
	struct nvme_completion	cqe;
};

struct nvme_tcp_data_pdu {
	struct nvme_tcp_common_hdr c_hdr;
	__u16			cccid;
	__u16			ttag;
	__le32			data_offset;
	__le32			data_length;
	__u8			rsvd[4];
};

union nvme_tcp_pdu {
	struct nvme_tcp_icreq_pdu		conn_req;
	struct nvme_tcp_icresp_pdu		conn_rep;
	struct nvme_tcp_cmd_capsule_pdu		cmd;
	struct nvme_tcp_resp_capsule_pdu	comp;
	struct nvme_tcp_data_pdu		data;
};

#endif /* __NVME_TCP_H__ */

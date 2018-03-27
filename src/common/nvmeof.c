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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <uuid/uuid.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <rdma/fabric.h>

#include "common.h"
#include "ops.h"
#include "linux/nvme-rdma.h"

#define NVME_CTRL_ENABLE	0x460001
#define NVME_CTRL_DISABLE	0x464001

#define NVME_DISC_KATO		360000 /* ms = minutes */
#define RETRY_COUNT		5
#define MSG_TIMEOUT		100
#define CONFIG_TIMEOUT		50
#define CONFIG_RETRY_COUNT	20
#define CONNECT_RETRY_COUNT	10

void dump(u8 *buf, int len)
{
	int			 i, j, n = 0;
	char			 hex[49];
	char			 prev[49];
	char			 chr[17];
	char			*p, *c;

	memset(prev, 0, sizeof(prev));
	memset(hex, 0, sizeof(hex));
	memset(chr, 0, sizeof(chr));
	c = chr;
	p = hex;

	for (i = j = 0; i < len; i++) {
		sprintf(p, "%02x ", buf[i]);
		p += 3;
		*c++ = (buf[i] >= 0x20 && buf[i] <= 0x7f) ? buf[i] : '.';

		if (++j == 16) {
			if (strcmp(hex, prev)) {
				if (n) {
					printf("----  repeated %d %s  ----\n",
					       n, n == 1 ? "time" : "times");
					n = 0;
				}
				printf("%04x  %s  %s\n", i - j + 1, hex, chr);
				strcpy(prev, hex);
			} else
				n++;
			j = 0;
			memset(hex, 0, sizeof(hex));
			memset(chr, 0, sizeof(chr));
			c = chr;
			p = hex;
		}
	}

	if (j) {
		if (strcmp(hex, prev) == 0)
			n++;
		if (n)
			printf("----  repeated %d %s  ----\n",
			       n, n == 1 ? "time" : "times");
		if (strcmp(hex, prev))
			printf("%04x  %-48s  %s\n", i - j, hex, chr);
	}
}

struct {
	int			 status;
	char			*str;
} nvme_status_array[] = {
	{ NVME_SC_INVALID_OPCODE,	"NVME_SC_INVALID_OPCODE" },
	{ NVME_SC_INVALID_FIELD,	"NVME_SC_INVALID_FIELD" },
	{ NVME_SC_CMDID_CONFLICT,	"NVME_SC_CMDID_CONFLICT" },
	{ NVME_SC_DATA_XFER_ERROR,	"NVME_SC_DATA_XFER_ERROR" },
	{ NVME_SC_POWER_LOSS,		"NVME_SC_POWER_LOSS" },
	{ NVME_SC_INTERNAL,		"NVME_SC_INTERNAL" },
	{ NVME_SC_ABORT_REQ,		"NVME_SC_ABORT_REQ" },
	{ NVME_SC_ABORT_QUEUE,		"NVME_SC_ABORT_QUEUE" },
	{ NVME_SC_FUSED_FAIL,		"NVME_SC_FUSED_FAIL" },
	{ NVME_SC_FUSED_MISSING,	"NVME_SC_FUSED_MISSING" },
	{ NVME_SC_INVALID_NS,		"NVME_SC_INVALID_NS" },
	{ NVME_SC_CMD_SEQ_ERROR,	"NVME_SC_CMD_SEQ_ERROR" },
	{ NVME_SC_SGL_INVALID_LAST,	"NVME_SC_SGL_INVALID_LAST" },
	{ NVME_SC_SGL_INVALID_COUNT,	"NVME_SC_SGL_INVALID_COUNT" },
	{ NVME_SC_SGL_INVALID_DATA,	"NVME_SC_SGL_INVALID_DATA" },
	{ NVME_SC_SGL_INVALID_METADATA,	"NVME_SC_SGL_INVALID_METADATA" },
	{ NVME_SC_SGL_INVALID_TYPE,	"NVME_SC_SGL_INVALID_TYPE" },
	{ NVME_SC_SGL_INVALID_OFFSET,	"NVME_SC_SGL_INVALID_OFFSET" },
	{ NVME_SC_SGL_INVALID_SUBTYPE,	"NVME_SC_SGL_INVALID_SUBTYPE" },
	{ NVME_SC_LBA_RANGE,		"NVME_SC_LBA_RANGE" },
	{ NVME_SC_CAP_EXCEEDED,		"NVME_SC_CAP_EXCEEDED" },
	{ NVME_SC_NS_NOT_READY,		"NVME_SC_NS_NOT_READY" },
	{ NVME_SC_RESERVATION_CONFLICT,	"NVME_SC_RESERVATION_CONFLICT" },
	{ NVME_SC_CQ_INVALID,		"NVME_SC_CQ_INVALID" },
	{ NVME_SC_QID_INVALID,		"NVME_SC_QID_INVALID" },
	{ NVME_SC_QUEUE_SIZE,		"NVME_SC_QUEUE_SIZE" },
	{ NVME_SC_ABORT_LIMIT,		"NVME_SC_ABORT_LIMIT" },
	{ NVME_SC_ABORT_MISSING,	"NVME_SC_ABORT_MISSING" },
	{ NVME_SC_ASYNC_LIMIT,		"NVME_SC_ASYNC_LIMIT" },
	{ NVME_SC_FIRMWARE_SLOT,	"NVME_SC_FIRMWARE_SLOT" },
	{ NVME_SC_FIRMWARE_IMAGE,	"NVME_SC_FIRMWARE_IMAGE" },
	{ NVME_SC_INVALID_VECTOR,	"NVME_SC_INVALID_VECTOR" },
	{ NVME_SC_INVALID_LOG_PAGE,	"NVME_SC_INVALID_LOG_PAGE" },
	{ NVME_SC_INVALID_FORMAT,	"NVME_SC_INVALID_FORMAT" },
	{ NVME_SC_FW_NEEDS_CONV_RESET,	"NVME_SC_FW_NEEDS_CONV_RESET" },
	{ NVME_SC_INVALID_QUEUE,	"NVME_SC_INVALID_QUEUE" },
	{ NVME_SC_FEATURE_NOT_SAVEABLE,	"NVME_SC_FEATURE_NOT_SAVEABLE" },
	{ NVME_SC_FEATURE_NOT_CHANGEABLE, "NVME_SC_FEATURE_NOT_CHANGEABLE" },
	{ NVME_SC_FEATURE_NOT_PER_NS,	"NVME_SC_FEATURE_NOT_PER_NS" },
	{ NVME_SC_FW_NEEDS_SUBSYS_RESET, "NVME_SC_FW_NEEDS_SUBSYS_RESET" },
	{ NVME_SC_FW_NEEDS_RESET,	"NVME_SC_FW_NEEDS_RESET" },
	{ NVME_SC_FW_NEEDS_MAX_TIME,	"NVME_SC_FW_NEEDS_MAX_TIME" },
	{ NVME_SC_FW_ACIVATE_PROHIBITED, "NVME_SC_FW_ACIVATE_PROHIBITED" },
	{ NVME_SC_OVERLAPPING_RANGE,	"NVME_SC_OVERLAPPING_RANGE" },
	{ NVME_SC_NS_INSUFFICENT_CAP,	"NVME_SC_NS_INSUFFICENT_CAP" },
	{ NVME_SC_NS_ID_UNAVAILABLE,	"NVME_SC_NS_ID_UNAVAILABLE" },
	{ NVME_SC_NS_ALREADY_ATTACHED,	"NVME_SC_NS_ALREADY_ATTACHED" },
	{ NVME_SC_NS_IS_PRIVATE,	"NVME_SC_NS_IS_PRIVATE" },
	{ NVME_SC_NS_NOT_ATTACHED,	"NVME_SC_NS_NOT_ATTACHED" },
	{ NVME_SC_THIN_PROV_NOT_SUPP,	"NVME_SC_THIN_PROV_NOT_SUPP" },
	{ NVME_SC_CTRL_LIST_INVALID,	"NVME_SC_CTRL_LIST_INVALID" },
	{ NVME_SC_BAD_ATTRIBUTES,	"NVME_SC_BAD_ATTRIBUTES" },
	{ NVME_SC_INVALID_PI,		"NVME_SC_INVALID_PI" },
	{ NVME_SC_READ_ONLY,		"NVME_SC_READ_ONLY" },
	{ NVME_SC_ONCS_NOT_SUPPORTED,	"NVME_SC_ONCS_NOT_SUPPORTED" },
	{ NVME_SC_CONNECT_FORMAT,	"NVME_SC_CONNECT_FORMAT" },
	{ NVME_SC_CONNECT_CTRL_BUSY,	"NVME_SC_CONNECT_CTRL_BUSY" },
	{ NVME_SC_CONNECT_INVALID_PARAM, "NVME_SC_CONNECT_INVALID_PARAM" },
	{ NVME_SC_CONNECT_RESTART_DISC,	"NVME_SC_CONNECT_RESTART_DISC" },
	{ NVME_SC_CONNECT_INVALID_HOST,	"NVME_SC_CONNECT_INVALID_HOST" },
	{ NVME_SC_DISCOVERY_RESTART,	"NVME_SC_DISCOVERY_RESTART" },
	{ NVME_SC_AUTH_REQUIRED,	"NVME_SC_AUTH_REQUIRED" },
	{ NVME_SC_WRITE_FAULT,		"NVME_SC_WRITE_FAULT" },
	{ NVME_SC_READ_ERROR,		"NVME_SC_READ_ERROR" },
	{ NVME_SC_GUARD_CHECK,		"NVME_SC_GUARD_CHECK" },
	{ NVME_SC_APPTAG_CHECK,		"NVME_SC_APPTAG_CHECK" },
	{ NVME_SC_REFTAG_CHECK,		"NVME_SC_REFTAG_CHECK" },
	{ NVME_SC_COMPARE_FAILED,	"NVME_SC_COMPARE_FAILED" },
	{ NVME_SC_ACCESS_DENIED,	"NVME_SC_ACCESS_DENIED" },
	{ NVME_SC_UNWRITTEN_BLOCK,	"NVME_SC_UNWRITTEN_BLOCK" },
};

static inline char *nvme_str_status(u16 _status)
{
	int			 i;
	u16			 status = _status & 0x3fff;
	static char		 str[80] = { 0 };

	for (i = 0; i < (int) ARRAY_SIZE(nvme_status_array); i++)
		if (nvme_status_array[i].status == status) {
			strcpy(str, nvme_status_array[i].str);
			break;
		}

	if (_status & NVME_SC_DNR)
		strcpy(str + strlen(str), " | NVME_SC_DNR");

	return str;
}

static inline void put_unaligned_le24(u32 val, u8 *p)
{
	*p++ = val & 0xff;
	*p++ = (val >> 8) & 0xff;
	*p++ = (val >> 16) & 0xff;
}

static inline void put_unaligned_le32(u32 val, u8 *p)
{
	*p++ = val & 0xff;
	*p++ = (val >> 8) & 0xff;
	*p++ = (val >> 16) & 0xff;
	*p++ = (val >> 24) & 0xff;
}

static inline int post_cmd(struct endpoint *ep, struct nvme_command *cmd,
			   int bytes)
{
	return ep->ops->post_msg(ep->ep, cmd, bytes, ep->mr);
}

static inline int send_cmd(struct endpoint *ep, struct nvme_command *cmd,
			   int bytes)
{
	return ep->ops->send_msg(ep->ep, cmd, bytes, ep->mr);
}

static int process_nvme_rsp(struct endpoint *ep, int ignore_status)
{
	struct xp_qe		*qe;
	struct nvme_completion	*rsp;
	struct timeval		 t0;
	int			 bytes;
	int			 ret;

	gettimeofday(&t0, NULL);

	while (1) {
		ret = ep->ops->poll_for_msg(ep->ep, &qe, (void **) &rsp,
					    &bytes);
		if (!ret)
			break;

		if (ret != -EAGAIN)
			return ret;

		if (stopped)
			return -ESHUTDOWN;

		if (msec_delta(t0) > MSG_TIMEOUT)
			return -EAGAIN;
	}

	if (bytes != sizeof(*rsp))
		return -EINVAL;

	ret = rsp->status >> 1;

	if (ret && ret != ignore_status)
		print_err("error status %s (0x%x)", nvme_str_status(ret), ret);

	ep->ops->repost_recv(ep->ep, qe);

	return ret;
}

static int send_fabric_connect(struct ctrl_queue *ctrl)
{
	struct endpoint		*ep = &ctrl->ep;
	struct nvmf_connect_data *data;
	struct nvme_keyed_sgl_desc *sg;
	struct nvme_command	*cmd = ep->cmd;
	int			 bytes;
	int			 key;
	int			 ret;
	int			 ignore_status;

	data = ep->data;
	key = ep->ops->remote_key(ep->data_mr);

	bytes = sizeof(*cmd);
	memset(cmd, 0, bytes);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->connect.opcode	= nvme_fabrics_command;
	cmd->connect.fctype	= nvme_fabrics_type_connect;
	cmd->connect.qid	= htole16(0);
	cmd->connect.sqsize	= htole16(NVMF_DQ_DEPTH);

	if (!ctrl->failed_kato)
		cmd->connect.kato = htole16(NVME_DISC_KATO);

	data->cntlid = htole16(NVME_CNTLID_DYNAMIC);
	strncpy(data->subsysnqn, NVME_DISC_SUBSYS_NAME, NVMF_NQN_SIZE);
	strncpy(data->hostnqn, ctrl->hostnqn, NVMF_NQN_SIZE);

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(sizeof(*data), sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		return ret;

	ignore_status = NVME_SC_DNR | NVME_SC_INVALID_FIELD;

	ret = process_nvme_rsp(ep, ignore_status);
	if (ret != ignore_status)
		return ret;

	cmd->connect.kato = 0;
	ctrl->failed_kato = 1;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		return ret;

	return process_nvme_rsp(ep, 0);
}

int send_keep_alive(struct endpoint *ep)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 bytes;
	int				 key;
	int				 ret;

	data = ep->data;
	key = ep->ops->remote_key(ep->data_mr);

	bytes = sizeof(*cmd);
	memset(cmd, 0, bytes);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_admin_keep_alive;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(4, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		return ret;

	process_nvme_rsp(ep, 0);

	return 0;
}

int send_get_config(struct endpoint *ep, int config_id, void **_data)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct xp_mr			*mr;
	u64				*data;
	int				 bytes;
	int				 key;
	int				 cnt = CONFIG_RETRY_COUNT;
	int				 ret;

	if (!cmd)
		return -EINVAL;

	bytes = sizeof(*cmd);

	if (posix_memalign((void **) &data, PAGE_SIZE, PAGE_SIZE)) {
		print_err("no memory for buffer, errno %d", errno);
		return errno;
	}

	memset(data, 0, PAGE_SIZE);

	ret = ep->ops->alloc_key(ep->ep, data, PAGE_SIZE, &mr);
	if (ret)
		return ret;

	key = ep->ops->remote_key(mr);

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;

	cmd->get_config.config_id	= config_id;
	cmd->get_config.fctype		= nvme_fabrics_type_get_config;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(PAGE_SIZE, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	*_data = data;

	ret = send_cmd(ep, cmd, bytes);
	if (ret) {
		free(data);
		goto out;
	}

	do {
		usleep(CONFIG_TIMEOUT);
		ret = process_nvme_rsp(ep, 0);
	} while (ret == -EAGAIN && --cnt);
out:
	ep->ops->dealloc_key(mr);

	return ret;
}

int send_set_config(struct endpoint *ep, int config_id, int len, void *data)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct xp_mr			*mr;
	int				 bytes;
	int				 key;
	int				 cnt = CONFIG_RETRY_COUNT;
	int				 ret;

	if (!cmd)
		return -EINVAL;

	bytes = sizeof(*cmd);

	ret = ep->ops->alloc_key(ep->ep, data, len, &mr);
	if (ret)
		return ret;

	key = ep->ops->remote_key(mr);

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;

	cmd->set_config.config_id	= config_id;
	cmd->set_config.fctype		= nvme_fabrics_type_set_config;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(len, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		goto out;

	do {
		usleep(CONFIG_TIMEOUT);
		ret = process_nvme_rsp(ep, 0);
	} while (ret == -EAGAIN && --cnt);
out:
	ep->ops->dealloc_key(mr);

	return ret;
}

int send_get_subsys_usage(struct endpoint *ep, int len,
			  struct nvmf_subsys_usage_hdr *hdr)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct xp_mr			*mr;
	int				 bytes;
	int				 key;
	int				 ret;

	bytes = sizeof(*cmd);

	ret = ep->ops->alloc_key(ep->ep, hdr, len, &mr);
	if (ret)
		return ret;

	key = ep->ops->remote_key(mr);

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->fabrics.fctype	= nvme_fabrics_type_get_subsys_usage;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) hdr;
	put_unaligned_le24(len, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);

	ep->ops->dealloc_key(mr);
	if (ret)
		return ret;

	ret = process_nvme_rsp(ep, 0);

	return ret;
}

static int send_get_property(struct endpoint *ep, u32 reg)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 bytes;
	int				 key;
	int				 ret;

	data = ep->data;
	key = ep->ops->remote_key(ep->data_mr);

	bytes = sizeof(*cmd);
	memset(cmd, 0, bytes);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->prop_get.fctype	= nvme_fabrics_type_property_get;
	cmd->prop_get.attrib	= 1;
	cmd->prop_get.offset	= htole32(reg);

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	//  sg->length = 0 not sizeof(u64) - put_unaligned_le24(4, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		return ret;

	return process_nvme_rsp(ep, 0);
}

static void prep_set_property(struct endpoint *ep, u32 reg, u64 val)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 key;

	data = ep->data;
	key = ep->ops->remote_key(ep->data_mr);

	memset(cmd, 0, sizeof(*cmd));

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->prop_set.fctype	= nvme_fabrics_type_property_set;
	cmd->prop_set.offset	= htole32(reg);
	cmd->prop_set.value	= htole64(val);

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	// CAYTON HACK put_unaligned_le24(BUF_SIZE, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;
}

static int send_set_property(struct endpoint *ep, u32 reg, u64 val)
{
	struct nvme_command	*cmd = ep->cmd;
	int			 ret;

	prep_set_property(ep, reg, val);

	ret = send_cmd(ep, cmd, sizeof(*cmd));
	if (ret)
		return ret;

	return process_nvme_rsp(ep, 0);
}

static int post_set_property(struct endpoint *ep, u32 reg, u64 val)
{
	struct nvme_command	*cmd = ep->cmd;

	prep_set_property(ep, reg, val);

	return post_cmd(ep, cmd, sizeof(*cmd));
}

int send_get_log_page(struct endpoint *ep, int log_size,
		      struct nvmf_disc_rsp_page_hdr **log)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct xp_mr			*mr;
	u64				*data;
	int				 bytes;
	u32				 size;
	u16				 numdl;
	u16				 numdu;
	int				 key;
	int				 ret;

	bytes = sizeof(*cmd);

	if (posix_memalign((void **) &data, PAGE_SIZE, log_size)) {
		print_err("no memory for buffer, errno %d", errno);
		return errno;
	}

	memset(data, 0, log_size);

	ret = ep->ops->alloc_key(ep->ep, data, log_size, &mr);
	if (ret)
		return ret;

	key = ep->ops->remote_key(mr);

	memset(cmd, 0, BUF_SIZE);

	size  = htole32((log_size / 4) - 1);
	numdl = size & 0xffff;
	numdu = (size >> 16) & 0xffff;

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_admin_get_log_page;
	cmd->common.nsid	= 0;

	cmd->get_log_page.lid	= NVME_LOG_DISC;
	cmd->get_log_page.numdl = numdl;
	cmd->get_log_page.numdu = numdu;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(log_size, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	*log = (struct nvmf_disc_rsp_page_hdr *) data;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		free(data);
	else
		ret =  process_nvme_rsp(ep, 0);

	ep->ops->dealloc_key(mr);

	return ret;
}

void disconnect_endpoint(struct endpoint *ep, int shutdown)
{
	if (shutdown && (ep->state == CONNECTED))
		post_set_property(ep, NVME_REG_CC, NVME_CTRL_DISABLE);

	if (ep->mr)
		ep->ops->dealloc_key(ep->mr);

	if (ep->ep)
		ep->ops->destroy_endpoint(ep->ep);

	if (ep->qe)
		free(ep->qe);

	if (ep->cmd)
		free(ep->cmd);
}

void disconnect_ctrl(struct ctrl_queue *ctrl, int shutdown)
{
	disconnect_endpoint(&ctrl->ep, shutdown);

	ctrl->connected = 0;
}

static int build_connect_data(struct nvme_rdma_cm_req **req, char *hostnqn)
{
	struct nvme_rdma_cm_req	*priv;
	struct nvmf_connect_data *data;
	int			bytes = sizeof(*priv) + sizeof(*data);

	if (posix_memalign((void **) &priv, PAGE_SIZE, bytes)) {
		print_err("no memory for buffer, errno %d", errno);
		return errno;
	}

	memset(priv, 0, bytes);

	priv->recfmt = htole16(NVME_RDMA_CM_FMT_1_0);
	priv->hrqsize = htole16(NVMF_DQ_DEPTH);
	priv->hsqsize = htole16(NVMF_DQ_DEPTH);

	data = (void *) &priv[1];

	data->cntlid = htole16(NVME_CNTLID_DYNAMIC);

	strncpy(data->subsysnqn, NVME_DISC_SUBSYS_NAME, NVMF_NQN_SIZE);
	strncpy(data->hostnqn, hostnqn, NVMF_NQN_SIZE);

	*req = priv;

	return bytes;
}

int connect_ctrl(struct ctrl_queue *ctrl)
{
	struct portid		*portid = ctrl->portid;
	struct endpoint		*ep = &ctrl->ep;
	struct sockaddr		 dest = { 0 };
	struct sockaddr_in	*dest_in = (struct sockaddr_in *) &dest;
	struct sockaddr_in6	*dest_in6 = (struct sockaddr_in6 *) &dest;
	struct nvme_rdma_cm_req	*req;
	void			*cmd;
	void			*data;
	int			 bytes;
	int			 ret = 0;
	int			 cnt = CONNECT_RETRY_COUNT;

	if (strcmp(portid->family, "ipv4") == 0) {
		dest_in->sin_family = AF_INET;
		dest_in->sin_port = htons(portid->port_num);
		ret = inet_pton(AF_INET, portid->address, &dest_in->sin_addr);
	} else if (strcmp(portid->family, "ipv6") == 0) {
		dest_in6->sin6_family = AF_INET6;
		dest_in6->sin6_port = htons(portid->port_num);
		ret = inet_pton(AF_INET6, portid->address,
				&dest_in6->sin6_addr);
	}

	if (!ret)
		return -EINVAL;
	if (ret < 0)
		return errno;

	ep->depth = NVMF_DQ_DEPTH;

	ret = ep->ops->init_endpoint(&ep->ep, NVMF_DQ_DEPTH);
	if (ret)
		return ret;

	bytes = build_connect_data(&req, ctrl->hostnqn);

	do {
		usleep(CONFIG_TIMEOUT);
		ret = ep->ops->client_connect(ep->ep, &dest, req, bytes);
	} while (ret == -EAGAIN && --cnt);

	if (bytes)
		free(req);
	if (ret) {
		ep->ops->destroy_endpoint(ep->ep);
		return ret;
	}

	if (posix_memalign(&cmd, PAGE_SIZE, PAGE_SIZE))
		goto out;

	memset(cmd, 0, PAGE_SIZE);

	ret = ep->ops->alloc_key(ep->ep, cmd, PAGE_SIZE, &ep->mr);
	if (ret)
		goto out;

	ep->cmd = cmd;

	if (posix_memalign(&data, PAGE_SIZE, PAGE_SIZE)) {
		ret = -errno;
		goto out;
	}

	memset(data, 0, PAGE_SIZE);

	ret = ep->ops->alloc_key(ep->ep, data, PAGE_SIZE, &ep->data_mr);
	if (ret)
		goto out;

	ep->data = data;

	ret = send_fabric_connect(ctrl);
	if (ret)
		goto out;

	return send_set_property(ep, NVME_REG_CC, NVME_CTRL_ENABLE);
out:
	disconnect_endpoint(ep, 0);

	return ret;
}

int start_pseudo_target(struct host_iface *iface)
{
	struct sockaddr		 dest;
	int			 ret;

	if (strcmp(iface->family, "ipv4") == 0)
		ret = inet_pton(AF_INET, iface->address, &dest);
	else if (strcmp(iface->family, "ipv6") == 0)
		ret = inet_pton(AF_INET6, iface->address, &dest);
	if (!ret)
		return -EINVAL;
	if (ret < 0)
		return errno;

	if (strcmp(iface->type, "rdma") == 0)
		iface->ops = rdma_register_ops();
	else
		return -EINVAL;

	ret = iface->ops->init_listener(&iface->listener, iface->port);
	if (ret)
		return ret;

	return 0;
}

int run_pseudo_target(struct endpoint *ep, void *id)
{
	void			*cmd;
	void			*data;
	int			 ret;

	ret = ep->ops->create_endpoint(&ep->ep, id, NVMF_DQ_DEPTH);
	if (ret)
		return ret;

	ret = ep->ops->accept_connection(ep->ep);
	if (ret)
		goto err1;

	if (posix_memalign(&cmd, PAGE_SIZE, PAGE_SIZE)) {
		ret = -errno;
		goto err1;
	}

	memset(cmd, 0, PAGE_SIZE);

	ret = ep->ops->alloc_key(ep->ep, cmd, PAGE_SIZE, &ep->mr);
	if (ret)
		goto err2;


	if (posix_memalign(&data, PAGE_SIZE, PAGE_SIZE)) {
		ret = -errno;
		goto err3;
	}

	memset(data, 0, PAGE_SIZE);

	ret = ep->ops->alloc_key(ep->ep, data, PAGE_SIZE, &ep->data_mr);
	if (ret)
		goto err4;

	ep->cmd = cmd;
	ep->data = data;

	return 0;

err4:
	free(data);
err3:
	ep->ops->dealloc_key(ep->mr);
err2:
	free(cmd);
err1:
	ep->ops->destroy_endpoint(ep->ep);
	return ret;
}

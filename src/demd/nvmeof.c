/*
 * NVMe over Fabrics Distributed Endpoint Manager (NVMe-oF DEM).
 * Copyright (C) 2017 Intel Corp., Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <uuid/uuid.h>
#include <sys/socket.h>

#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_domain.h>
//#include <rdma/fi_tagged.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_rma.h>

#include "common.h"
#include "linux/nvme-rdma.h"

#define NVME_CTRL_ENABLE 0x460001
#define NVME_CTRL_DISABLE 0x464001

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

static int post_cmd(struct endpoint *ep, struct nvme_command *cmd, int bytes)
{
	int ret;

	ret = fi_send(ep->ep, cmd, bytes, fi_mr_desc(ep->send_mr),
		      FI_ADDR_UNSPEC, NULL);
	if (ret)
		print_err("fi_send returned %d", ret);

	return ret;
}

static int send_cmd(struct endpoint *ep, struct nvme_command *cmd, int bytes)
{
	struct fi_cq_err_entry	comp;
	int ret;

	ret = fi_send(ep->ep, cmd, bytes, fi_mr_desc(ep->send_mr),
		      FI_ADDR_UNSPEC, NULL);
	if (ret) {
		print_err("fi_send returned %d", ret);
		return ret;
	}

	while (!stopped) {
		ret = fi_cq_sread(ep->scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("fi_send failed");
		print_cq_error(ep->scq, ret);
		return ret;
	}

	while (!stopped) {
		ret = fi_cq_sread(ep->rcq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret == 1) {
			struct qe *qe = comp.op_context;

			ret = fi_recv(ep->ep, qe->buf, BUF_SIZE,
				      fi_mr_desc(qe->recv_mr), 0, qe);
			if (ret)
				print_err("fi_recv returned %d", ret);
			break;
		}
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("error on command %02x", cmd->common.opcode);
		print_cq_error(ep->rcq, ret);
		return ret;
	}
	return 0;
}

static int send_fabric_connect(struct endpoint *ep)
{
	struct nvmf_connect_data	*data;
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	uuid_t				 id;
	char				 uuid[40];
	int				 bytes;

	bytes = sizeof(*cmd);

	data = (void *) &cmd[1];

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->connect.opcode	= nvme_fabrics_command;
	cmd->connect.fctype	= nvme_fabrics_type_connect;
	cmd->connect.qid	= htole16(0);
	cmd->connect.sqsize	= htole16(NVMF_DQ_DEPTH);

	uuid_generate(id);
	memcpy(&data->hostid, id, sizeof(*id));
	uuid_unparse_lower(id, uuid);

	data->cntlid = htole16(0xffff);
	strncpy(data->subsysnqn, NVME_DISC_SUBSYS_NAME, NVMF_NQN_SIZE);
	snprintf(data->hostnqn, NVMF_NQN_SIZE,
		 "nqn.2014-08.org.nvmexpress:NVMf:uuid:%s", uuid);

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(sizeof(*data), sg->length);
	put_unaligned_le32(fi_mr_key(ep->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ep, cmd, bytes);
}

int send_keep_alive(struct endpoint *ep)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 bytes;

	bytes = sizeof(*cmd);

	data = (void *) &cmd[1];

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_admin_keep_alive;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(4, sg->length);
	put_unaligned_le32(fi_mr_key(ep->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ep, cmd, bytes);
}

int send_set_port_config(struct endpoint *ep, int len,
			 struct nvmf_port_config_page_hdr *hdr)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct fid_mr			*mr;
	int				 bytes;
	int				 ret;

	bytes = sizeof(*cmd);

	memset(cmd, 0, BUF_SIZE);

	ret = fi_mr_reg(ep->dom, hdr, len, FI_RECV | FI_SEND |
			FI_REMOTE_READ | FI_REMOTE_WRITE |
			FI_READ | FI_WRITE,
			0, 0, 0, &mr, NULL);
	if (ret) {
		print_err("fi_mr_reg returned %d", ret);
		return ret;
	}

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->fabrics.fctype	= nvme_fabrics_type_set_port_config;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) hdr;
	put_unaligned_le24(len, sg->length);
	put_unaligned_le32(fi_mr_key(mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	fi_close(&mr->fid);
	return ret;
}

int send_set_subsys_config(struct endpoint *ep, int len,
			   struct nvmf_subsys_config_page_hdr *hdr)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct fid_mr			*mr;
	int				 bytes;
	int				 ret;

	bytes = sizeof(*cmd);

	memset(cmd, 0, BUF_SIZE);

	ret = fi_mr_reg(ep->dom, hdr, len, FI_RECV | FI_SEND |
			FI_REMOTE_READ | FI_REMOTE_WRITE |
			FI_READ | FI_WRITE,
			0, 0, 0, &mr, NULL);
	if (ret) {
		print_err("fi_mr_reg returned %d", ret);
		return ret;
	}

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->fabrics.fctype	= nvme_fabrics_type_set_subsys_config;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) hdr;
	put_unaligned_le24(len, sg->length);
	put_unaligned_le32(fi_mr_key(mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	fi_close(&mr->fid);
	return ret;
}

int send_get_subsys_usage(struct endpoint *ep, int len,
			  struct nvmf_subsys_usage_rsp_page_hdr *hdr)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct fid_mr			*mr;
	int				 bytes;
	int				 ret;

	bytes = sizeof(*cmd);

	memset(cmd, 0, BUF_SIZE);

	ret = fi_mr_reg(ep->dom, hdr, len, FI_RECV | FI_SEND |
			FI_REMOTE_READ | FI_REMOTE_WRITE |
			FI_READ | FI_WRITE,
			0, 0, 0, &mr, NULL);
	if (ret) {
		print_err("fi_mr_reg returned %d", ret);
		return ret;
	}

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->fabrics.fctype	= nvme_fabrics_type_get_subsys_usage;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) hdr;
	put_unaligned_le24(len, sg->length);
	put_unaligned_le32(fi_mr_key(mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	fi_close(&mr->fid);
	return ret;
}

int send_get_property(struct endpoint *ep, u32 reg)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 bytes;

	bytes = sizeof(*cmd);

	data = (void *) &cmd[1];

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->prop_get.fctype	= nvme_fabrics_type_property_get;
	cmd->prop_get.attrib	= 1;
	cmd->prop_get.offset	= htole32(reg);

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(4, sg->length);
	put_unaligned_le32(fi_mr_key(ep->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ep, cmd, bytes);
}

static void prep_set_property(struct endpoint *ep, u32 reg, u64 val)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;

	data = (void *) &cmd[1];

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->prop_set.fctype	= nvme_fabrics_type_property_set;
	cmd->prop_set.offset	= htole32(reg);
	cmd->prop_set.value	= htole64(val);

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(BUF_SIZE, sg->length);
	put_unaligned_le32(fi_mr_key(ep->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;
}

static int send_set_property(struct endpoint *ep, u32 reg, u64 val)
{
	struct nvme_command		*cmd = ep->cmd;

	prep_set_property(ep, reg, val);

	return send_cmd(ep, cmd, sizeof(*cmd));
}

static int post_set_property(struct endpoint *ep, u32 reg, u64 val)
{
	struct nvme_command		*cmd = ep->cmd;

	prep_set_property(ep, reg, val);

	return post_cmd(ep, cmd, sizeof(*cmd));
}

int send_get_log_page(struct endpoint *ep, int log_size,
		      struct nvmf_disc_rsp_page_hdr **log)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct fid_mr			*mr;
	u64				*data;
	int				 bytes;
	u32				 size;
	u16				 numdl;
	u16				 numdu;
	int				 ret;

	bytes = sizeof(*cmd);

	data = alloc_buffer(ep, log_size, &mr);
	if (!data)
		return -ENOMEM;

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
	put_unaligned_le32(fi_mr_key(mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	*log = (struct nvmf_disc_rsp_page_hdr *) data;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		free(data);

	fi_close(&mr->fid);

	return ret;
}

void disconnect_target(struct endpoint *ep, int shutdown)
{
	if (shutdown && (ep->state == CONNECTED))
		post_set_property(ep, NVME_REG_CC, NVME_CTRL_DISABLE);

	cleanup_endpoint(ep, shutdown);

	if (ep->qe)
		free(ep->qe);
	if (ep->cmd)
		free(ep->cmd);
	if (ep->data)
		free(ep->data);
}

static int build_connect_data(struct nvme_rdma_cm_req **req)
{
	struct nvme_rdma_cm_req	*priv;
	struct nvmf_connect_data *data;
	uuid_t			id;
	char			uuid[40];
	int			bytes = sizeof(*priv) + sizeof(*data);

	priv = malloc(bytes);
	if (!priv)
		return 0;

	memset(priv, 0, bytes);

	priv->recfmt = htole16(NVME_RDMA_CM_FMT_1_0);
	priv->hrqsize = htole16(NVMF_DQ_DEPTH);
	priv->hsqsize = htole16(NVMF_DQ_DEPTH);

	data = (void *) &priv[1];

	uuid_generate(id);
	memcpy(&data->hostid, id, sizeof(*id));
	uuid_unparse_lower(id, uuid);

	data->cntlid = htole16(0xffff);
	strncpy(data->subsysnqn, NVME_DISC_SUBSYS_NAME, NVMF_NQN_SIZE);
	snprintf(data->hostnqn, NVMF_NQN_SIZE,
		 "nqn.2014-08.org.nvmexpress:NVMf:uuid:%s", uuid);

	*req = priv;

	return bytes;
}

int connect_target(struct endpoint *ep, char *type, char *node, char *port)
{
	struct nvme_command	*cmd;
	char			*provider;
	char			*verbs = "verbs";
	int			 ret;
	struct nvme_rdma_cm_req	*req;
	int			 bytes;

	if (!strcmp(type, "rdma"))
		provider = verbs;
	else
		return -EPROTONOSUPPORT;

	ep->depth = NVMF_DQ_DEPTH;

	ret = init_endpoint(ep, provider, node, port);
	if (ret)
		return ret;

	ret = create_endpoint(ep, ep->prov);
	if (ret)
		return ret;

	bytes = build_connect_data(&req);

	ret = client_connect(ep, req, bytes);

	if (bytes)
		free(req);
	if (ret)
		return ret;

	cmd = alloc_buffer(ep, BUF_SIZE, &ep->send_mr);
	if (!cmd)
		return -ENOMEM;

	ep->cmd = cmd;

	ret = send_fabric_connect(ep);
	if (ret)
		return ret;

	ret = send_set_property(ep, NVME_REG_CC, NVME_CTRL_ENABLE);

	return ret;
}

int start_pseudo_target(struct listener *pep, char *type, char *node,
			char *port)
{
	char			*provider;
	char			verbs[] = "verbs";
	int			ret;

	if (!strcmp(type, "rdma"))
		provider = verbs;
	else
		return -EPROTONOSUPPORT;

	ret = init_listener(pep, provider, node, port);
	if (ret)
		return ret;

	ret = server_listen(pep);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	cleanup_listener(pep);
	return ret;
}

int run_pseudo_target(struct endpoint *ep)
{
	struct nvme_command	*cmd;
	void			*data;
	int			 ret;

	ret = init_fabric(ep);
	if (ret)
		goto out;

	ret = create_endpoint(ep, ep->prov);
	if (ret)
		goto out;

	ret = accept_connection(ep);
	if (ret)
		goto out;

	cmd = alloc_buffer(ep, BUF_SIZE, &ep->send_mr);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ep->cmd = cmd;

	data = alloc_buffer(ep, BUF_SIZE, &ep->data_mr);
	if (!data) {
		ret = -ENOMEM;
		goto out;
	}

	ep->data = data;

	ret = 0;
out:
	return ret;
}

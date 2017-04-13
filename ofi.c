/*
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
#include "nvme.h"
#include "nvme-rdma.h"

#define IDLE_TIMEOUT 100
#define PAGE_SIZE 4096

#define NVME_CTRL_ENABLE 0x460001
#define NVME_CTRL_DISABLE 0x464001

void dump(u8 *buf, int len)
{
	int i, j;

	for (i = j = 0; i < len; i++) {
		printf("%02x ", buf[i]);
		if (++j == 16) printf("\n"), j= 0;
	}
	if (j) printf("\n");
}

void print_cq_error(struct fid_cq *cq, int n)
{
	int rc;
	struct fi_cq_err_entry entry = { 0 };

	if (n < 0)
		n = -n;

	if (n != FI_EAVAIL)
		print_err("fi_cq_sread '%s'", fi_strerror(n));

	rc = fi_cq_readerr(cq, &entry, 0);
	if (rc < 0)
		print_err("fi_cq_readerr() returns %d '%s'",
			  rc, fi_strerror(rc));
	else {
		char buf[64];

		print_err("fi_cq_readerr() prov_err '%s'(%d)",
			  fi_cq_strerror(cq, entry.prov_errno, entry.err_data,
					 buf, sizeof(buf)),
			  entry.prov_errno);
		print_err("fi_cq_readerr() err '%s'", fi_strerror(entry.err));
	}
}

void print_eq_error(struct fid_eq *eq, int n)
{
	int rc;
	struct fi_eq_err_entry eqe = { 0 };

	if (n < 0)
		n = -n;

	if (n != FI_EAVAIL)
		print_err("fi_eq_sread '%s'", fi_strerror(n));

	rc = fi_eq_readerr(eq, &eqe, 0);
	if (rc < 0)
		print_err("fi_eq_readerr() returns %d '%s'",
			  rc, fi_strerror(rc));
	else {
		char buf[64];

		print_err("fi_eq_readerr() prov_err '%s'(%d)",
			  fi_eq_strerror(eq, eqe.prov_errno, eqe.err_data,
					 buf, sizeof(buf)),
			  eqe.prov_errno);
		print_err("fi_eq_readerr() err '%s'", fi_strerror(eqe.err));
	}
}

static void *alloc_buffer(struct context *ctx, int size, struct fid_mr **mr)
{
	void			*buf;
	int			ret;

	if (posix_memalign(&buf, PAGE_SIZE, size)) {
		print_err("no memory for buffer, errno %d", errno);
		goto out;
	}
	ret = fi_mr_reg(ctx->dom, buf, size, FI_RECV | FI_SEND |
			FI_REMOTE_READ | FI_REMOTE_WRITE |
			FI_READ | FI_WRITE,
			0, 0, 0, mr, NULL);
	if (ret) {
		print_err("fi_mr_reg returned %d", ret);
		free(buf);
		goto out;
	}

	return buf;
out:
	*mr = NULL;
	return NULL;
}

static int init_fabrics(struct context *ctx, uint64_t flags, char *provider,
			char *node, char *srvc)
{
	struct fi_info		*hints;
	struct qe		*qe;
	int			i;
	int			ret;

	ctx->state		= DISCONNECTED;

	hints = fi_allocinfo();
	if (!hints) {
		print_err("no memory for hints");
		return -ENOMEM;
	}

	hints->caps			= FI_MSG;
	hints->mode			= FI_LOCAL_MR;

	hints->ep_attr->type		= FI_EP_MSG;
	hints->ep_attr->protocol	= FI_PROTO_UNSPEC;

	hints->fabric_attr->prov_name	= strdup(provider);

	ret = fi_getinfo(FI_VERSION(1, 0), node, srvc, flags, hints,
			 &ctx->prov);
	fi_freeinfo(hints);

	if (ret) {
		print_err("fi_getinfo() returned %d", ret);
		return ret;
	}
	if (!ctx->prov) {
		print_err("No matching provider found?");
		return -EINVAL;
	}
	ret = fi_fabric(ctx->prov->fabric_attr, &ctx->fab, NULL);
	if (ret) {
		print_err("fi_fabric returned %d", ret);
		goto free_info;
	}
	ret = fi_domain(ctx->fab, ctx->prov, &ctx->dom, NULL);
	if (ret) {
		print_err("fi_domain returned %d", ret);
		goto close_fab;
	}

	qe = calloc(sizeof(struct qe), NVMF_DQ_DEPTH);
	if (!qe)
		return -ENOMEM;

	ctx->qe = qe;

	for (i = 0; i < NVMF_DQ_DEPTH; i++) {
		qe[i].buf = alloc_buffer(ctx, BUF_SIZE, &qe[i].recv_mr);
		if (!qe[i].buf)
			return ret;
	}

	return 0;
close_fab:
	fi_close(&ctx->fab->fid);
	ctx->fab = NULL;
free_info:
	fi_freeinfo(ctx->prov);
	ctx->prov = NULL;

	return ret;
}

static int server_listen(struct context *ctx)
{
	struct fi_eq_attr	eq_attr = { 0 };
	int			ret;

	ret = fi_eq_open(ctx->fab, &eq_attr, &ctx->peq, NULL);
	if (ret) {
		print_err("fi_eq_open returned %d", ret);
		return ret;
	}
	ret = fi_passive_ep(ctx->fab, ctx->prov, &ctx->pep, ctx);
	if (ret) {
		print_err("fi_passive_ep returned %d", ret);
		return ret;
	}
	ret = fi_pep_bind(ctx->pep, &ctx->peq->fid, 0);
	if (ret) {
		print_err("fi_pep_bind returned %d", ret);
		return ret;
	}
	ret = fi_listen(ctx->pep);
	if (ret) {
		print_err("fi_listen returned %d", ret);
		return ret;
	}

	stopped = 0;

	return 0;
}

int pseudo_target_check_for_host(struct context *ctx)
{
	struct fi_eq_cm_entry	entry;
	uint32_t		event;
	int			ret;

	while (!stopped) {
		ret = fi_eq_sread(ctx->peq, &event, &entry, sizeof(entry),
				  CTIMEOUT, 0);
		if (ret == sizeof(entry))
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			return ret;
		print_eq_error(ctx->peq, ret);
		return ret;
	}

	if (stopped)
		return -ESHUTDOWN;

	if (event != FI_CONNREQ) {
		print_err("Unexpected CM event %d", event);
		return -FI_EOTHER;
	}

	ctx->info = entry.info;

	return 0;
}

static int accept_connection(struct context *ctx)
{
	struct fi_eq_cm_entry	entry;
	uint32_t		event;
	int			ret;
	int			i;

	for (i = 0; i < NVMF_DQ_DEPTH; i++) {
		ret = fi_recv(ctx->ep, ctx->qe[i].buf, BUF_SIZE,
			      fi_mr_desc(ctx->qe[i].recv_mr), 0, &ctx->qe[i]);
		if (ret) {
			print_err("fi_recv returned %d", ret);
			return ret;;
		}
	}

	ret = fi_accept(ctx->ep, NULL, 0);
	if (ret) {
		print_err("fi_accept returned %d", ret);
		return ret;
	}

	ret = fi_eq_sread(ctx->eq, &event, &entry, sizeof(entry),
			  CTIMEOUT, 0);
	if (ret != sizeof(entry)) {
		print_eq_error(ctx->eq, ret);
		return ret;
	}

	if (event != FI_CONNECTED) {
		print_err("unexpected event %d", event);
		return -FI_EOTHER;
	}

	ctx->state = CONNECTED;

	return 0;
}

static int create_endpoint(struct context *ctx, struct fi_info *info)
{
	struct fi_eq_attr	eq_attr = { 0 };
	struct fi_cq_attr	cq_attr = { 0 };
	int			ret;

	info->ep_attr->tx_ctx_cnt = 1;
	info->ep_attr->rx_ctx_cnt = 2;
	info->tx_attr->iov_limit = 1;
	info->rx_attr->iov_limit = 1;
	info->tx_attr->inject_size = 0;

	cq_attr.size = NVMF_DQ_DEPTH;
	cq_attr.format = FI_CQ_FORMAT_MSG;
	cq_attr.wait_obj = FI_WAIT_UNSPEC;
	cq_attr.wait_cond = FI_CQ_COND_NONE;

	ret = fi_cq_open(ctx->dom, &cq_attr, &ctx->rcq, NULL);
	if (ret) {
		print_err("fi_cq_open for rcq returned %d", ret);
		return ret;
	}
	ret = fi_cq_open(ctx->dom, &cq_attr, &ctx->scq, NULL);
	if (ret) {
		print_err("fi_cq_open for scq returned %d", ret);
		return ret;
	}
	ret = fi_eq_open(ctx->fab, &eq_attr, &ctx->eq, NULL);
	if (ret) {
		print_err("fi_eq_open returned %d", ret);
		return ret;
	}
	ret = fi_endpoint(ctx->dom, info, &ctx->ep, ctx);
	if (ret) {
		print_err("fi_endpoint returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ctx->ep, &ctx->rcq->fid, FI_RECV);
	if (ret) {
		print_err("fi_ep_bind for rcq returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ctx->ep, &ctx->scq->fid, FI_SEND);
	if (ret) {
		print_err("fi_ep_bind for scq returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ctx->ep, &ctx->eq->fid, 0);
	if (ret) {
		print_err("fi_ep_bind for eq returned %d", ret);
		return ret;
	}
	ret = fi_enable(ctx->ep);
	if (ret) {
		print_err("fi_enable returned %d", ret);
		return ret;
	}

	return 0;
}

int client_connect(struct context *ctx)
{
	struct fi_eq_cm_entry	entry;
	uint32_t		event;
	struct nvme_rdma_cm_req	*priv;
	struct nvmf_connect_data *data;
	uuid_t			id;
	char			uuid[40];
	int			bytes = sizeof(*priv) + sizeof(*data);
	int			i;
	int			ret;

	priv = malloc(bytes);
	if (!priv)
		return -ENOMEM;

	for (i = 0; i < NVMF_DQ_DEPTH; i++) {
		ret = fi_recv(ctx->ep, ctx->qe[i].buf, BUF_SIZE,
			      fi_mr_desc(ctx->qe[i].recv_mr), 0, &ctx->qe[i]);
		if (ret) {
			print_err("fi_recv returned %d", ret);
			return ret;;
		}
	}

	memset(priv, 0, bytes);

	priv->recfmt = htole16(NVME_RDMA_CM_FMT_1_0);
	priv->hrqsize = htole16(NVMF_DQ_DEPTH);
	priv->hsqsize = htole16(NVMF_DQ_DEPTH - 1);

	data = (void *) &priv[1];

	uuid_generate(id);
	memcpy(&data->hostid, id, sizeof(*id));
	uuid_unparse_lower(id, uuid);

	data->cntlid = htole16(0xffff);
	strncpy(data->subsysnqn, NVME_DISC_SUBSYS_NAME, NVMF_NQN_SIZE);
	snprintf(data->hostnqn, NVMF_NQN_SIZE,
		 "nqn.2014-08.org.nvmexpress:NVMf:uuid:%s", uuid);

	ret = fi_connect(ctx->ep, ctx->prov->dest_addr, priv, bytes);
	if (ret) {
		print_err("fi_connect returned %d", ret);
		goto out;
	}

	while (!stopped) {
		ret = fi_eq_sread(ctx->eq, &event, &entry, sizeof(entry),
				  IDLE_TIMEOUT, 0);
		if (ret == sizeof(entry))
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_eq_error(ctx->eq, ret);
		goto out;
	}

	if (event != FI_CONNECTED) {
		print_err("fi_connect failed, event %d", event);
		ret = -ECONNRESET;
		goto out;
	}

	ctx->state = CONNECTED;

	ret = 0;

out:
	free(priv);
	return ret;
}

void cleanup_fabric(struct context *ctx)
{
	int had_an_ep = 0;

	if (ctx->state != DISCONNECTED)
		fi_shutdown(ctx->ep, 0);

	if (ctx->send_mr) {
		fi_close(&ctx->send_mr->fid);
		ctx->send_mr = NULL;
	}
	if (ctx->ep) {
		fi_close(&ctx->ep->fid);
		ctx->ep = NULL;
		had_an_ep = 1;
	}
	if (ctx->rcq) {
		fi_close(&ctx->rcq->fid);
		ctx->rcq = NULL;
	}
	if (ctx->scq) {
		fi_close(&ctx->scq->fid);
		ctx->scq = NULL;
	}
	if (ctx->eq) {
		fi_close(&ctx->eq->fid);
		ctx->eq = NULL;
	}

	/* if a pseudo target is connected to a host, stop here and
	 * do not cleanup the listening part of the context yet
	 */
	if (ctx->pep && had_an_ep)
		return;

	if (ctx->info) {
		fi_freeinfo(ctx->info);
		ctx->info = NULL;
	}
	if (ctx->pep) {
		fi_close(&ctx->pep->fid);
		ctx->pep = NULL;
	}
	if (ctx->peq) {
		fi_close(&ctx->peq->fid);
		ctx->peq = NULL;
	}
	if (ctx->dom) {
		fi_close(&ctx->dom->fid);
		ctx->dom = NULL;
	}
	if (ctx->fab && ctx->pep) {
		fi_close(&ctx->fab->fid);
		ctx->fab = NULL;
	}
	if (ctx->prov) {
		fi_freeinfo(ctx->prov);
		ctx->prov = NULL;
	}

	ctx->state = DISCONNECTED;
}

static inline void put_unaligned_le24(u32 val, u8 *p)
{
	*p++ = val;
	*p++ = val >> 8;
	*p++ = val >> 16;
}

static inline void put_unaligned_le32(u32 val, u8 *p)
{
	*p++ = val;
	*p++ = val >> 8;
	*p++ = val >> 16;
	*p++ = val >> 24;
}

static int send_cmd(struct context *ctx, struct nvme_command *cmd, int bytes)
{
	struct fi_cq_err_entry	comp;
	int ret;

	ret = fi_send(ctx->ep, cmd, bytes, fi_mr_desc(ctx->send_mr),
		      FI_ADDR_UNSPEC, NULL);
	if (ret) {
		print_err("fi_send returned %d", ret);
		return ret;
	}

	while (!stopped) {
		ret = fi_cq_sread(ctx->scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("fabric connect failed");
		print_cq_error(ctx->scq, ret);
		return ret;
	}

	while (!stopped) {
		ret = fi_cq_sread(ctx->rcq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret == 1) {
			struct qe *qe = comp.op_context;
			//print_info("completed recv");
			//dump(qe->buf, comp.len);
			//memset(qe->buf, 0, BUF_SIZE);
			ret = fi_recv(ctx->ep, qe->buf, BUF_SIZE,
				      fi_mr_desc(qe->recv_mr), 0, qe);
			if (ret)
				print_err("fi_recv returned %d", ret);
			break;
		}
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("error on command %02x", cmd->common.opcode);
		print_cq_error(ctx->rcq, ret);
		return ret;
	}
	return 0;
}

static int send_fabric_connect(struct context *ctx)
{
	struct nvmf_connect_data	*data;
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ctx->cmd;
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
	put_unaligned_le32(fi_mr_key(ctx->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ctx, cmd, bytes);
}

static int send_get_property(struct context *ctx, u32 reg)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ctx->cmd;
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
	put_unaligned_le32(fi_mr_key(ctx->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ctx, cmd, bytes);
}

static int send_set_property(struct context *ctx, u32 reg, u64 val)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ctx->cmd;
	u64				*data;
	int				 bytes;

	bytes = sizeof(*cmd);

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
	put_unaligned_le32(fi_mr_key(ctx->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ctx, cmd, bytes);
}

int send_get_log_page(struct context *ctx, int log_size,
		      struct nvmf_disc_rsp_page_hdr **log)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ctx->cmd;
	struct fid_mr			*mr;
	u64				*data;
	int				 bytes;
	u32				 size;
	u16				 numdl;
	u16				 numdu;
	int				 ret;

	bytes = sizeof(*cmd);

	data = alloc_buffer(ctx, log_size, &mr);
	if (!data)
		return -ENOMEM;

	memset(cmd, 0, BUF_SIZE);

	size  = htole32((log_size / 4) -1);
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

	ret = send_cmd(ctx, cmd, bytes);
	if (ret)
		free(data);

	fi_close(&mr->fid);

	return ret;
}

int rma_read(struct fid_ep *ep, struct fid_cq *scq, void *buf, int len,
	     void *desc, u64 addr, u64 key)
{
	struct fi_cq_err_entry	comp;
	int			ret;

	ret = fi_read(ep, buf, len, desc, (fi_addr_t) NULL, addr, key, NULL);
	if (ret)
		return ret;

	while (!stopped) {
		ret = fi_cq_sread(scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			return 0;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("fabric connect failed");
		print_cq_error(scq, ret);
		break;
	}

	return ret;
}

int rma_write(struct fid_ep *ep, struct fid_cq *scq, void *buf, int len,
	      void *desc, u64 addr, u64 key)
{
	struct fi_cq_err_entry	comp;
	int			ret;

	ret = fi_write(ep, buf, len, desc, (fi_addr_t) NULL, addr, key, NULL);
	if (ret)
		return ret;

	while (!stopped) {
		ret = fi_cq_sread(scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			return 0;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("fabric connect failed");
		print_cq_error(scq, ret);
		break;
	}

	return ret;
}

int send_msg_and_repost(struct context *ctx, struct qe *qe, void *msg, int len)
{
	struct fi_cq_err_entry comp;
	int ret;

	ret = fi_recv(ctx->ep, qe->buf, BUF_SIZE, fi_mr_desc(qe->recv_mr),
		      0, qe);
	if (ret) {
		print_err("fi_recv returned %d", ret);
		return ret;
	}

	ret = fi_send(ctx->ep, msg, len, fi_mr_desc(ctx->send_mr),
		      FI_ADDR_UNSPEC, NULL);
	if (ret) {
		print_err("fi_send returned %d", ret);
		return ret;
	}

	while (!stopped) {
		ret = fi_cq_sread(ctx->scq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_err("fabric connect failed");
		print_cq_error(ctx->scq, ret);
		return ret;
	}

	return 0;
}

void disconnect_controller(struct context *ctx)
{
	if (ctx->state == CONNECTED)
		send_set_property(ctx, NVME_REG_CC, NVME_CTRL_DISABLE);

	cleanup_fabric(ctx);

	if (ctx->qe)
		free(ctx->qe);
	if (ctx->cmd)
		free(ctx->cmd);
	if (ctx->data)
		free(ctx->data);
}

int connect_controller(struct context *ctx, char *type, char *node, char *port)
{
	struct nvme_command	*cmd;
	char			*provider;
	char			*verbs = "verbs";
	int			ret;

	if (!strcmp(type, "rdma"))
		provider = verbs;
	else
		return -EPROTONOSUPPORT;

	ret = init_fabrics(ctx, 0, provider, node, port);
	if (ret)
		return ret;

	ret = create_endpoint(ctx, ctx->prov);
	if (ret)
		return ret;;

	ret = client_connect(ctx);
	if (ret)
		return ret;;

	cmd = alloc_buffer(ctx, BUF_SIZE, &ctx->send_mr);
	if (!cmd)
		return -ENOMEM;

	ctx->cmd = cmd;

	ret = send_fabric_connect(ctx);
	if (ret)
		return ret;

	ret = send_set_property(ctx, NVME_REG_CC, NVME_CTRL_ENABLE);

	return ret;
}

int start_pseudo_target(struct context *ctx, char *type, char *node, char *port)
{
	char			*provider;
	char			verbs[] = "verbs";
	int			ret;

	if (!strcmp(type, "rdma"))
		provider = verbs;
	else
		return -EPROTONOSUPPORT;

	ret = init_fabrics(ctx, FI_SOURCE, provider, node, port);
	if (ret)
		return ret;

	ret = server_listen(ctx);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	cleanup_fabric(ctx);
	return ret;
}

int run_pseudo_target(struct context *ctx)
{
	struct nvme_command	*cmd;
	void			*data;
	int			 ret;

	ret = create_endpoint(ctx, ctx->info);
	if (ret)
		goto cleanup;

	ret = accept_connection(ctx);
	if (ret)
		goto cleanup;

	cmd = alloc_buffer(ctx, BUF_SIZE, &ctx->send_mr);
	if (!cmd)
		return -ENOMEM;

	ctx->cmd = cmd;

	data = alloc_buffer(ctx, BUF_SIZE, &ctx->data_mr);
	if (!data)
		return -ENOMEM;

	ctx->data = data;

	return 0;

cleanup:
	cleanup_fabric(ctx);
	return ret;
}

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
#include "incl/nvme-rdma.h"

#define IDLE_TIMEOUT 100
#define PAGE_SIZE 4096

#define NVME_CTRL_ENABLE 0x460001
#define NVME_CTRL_DISABLE 0x464001

void dump(u8 *buf, int len)
{
	int			 i, j;

	for (i = j = 0; i < len; i++) {
		printf("%02x ", buf[i]);
		if (++j == 16) {
			printf("\n");
			j = 0;
		}
	}

	if (j)
		printf("\n");
}

void print_cq_error(struct fid_cq *cq, int n)
{
	int			 rc;
	struct fi_cq_err_entry	 entry = { NULL };

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

static void print_eq_error(struct fid_eq *eq, int n)
{
	int			 rc;
	struct fi_eq_err_entry	 eqe = { NULL };

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

void *alloc_buffer(struct endpoint *ep, int size, struct fid_mr **mr)
{
	void			*buf;
	int			 ret;

	if (posix_memalign(&buf, PAGE_SIZE, size)) {
		print_err("no memory for buffer, errno %d", errno);
		goto out;
	}
	memset(buf, 0, size);
	ret = fi_mr_reg(ep->dom, buf, size, FI_RECV | FI_SEND |
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

static int init_fabric(struct endpoint *ep)
{
	struct qe		*qe;
	int			 i;
	int			 ret;

	ret = fi_fabric(ep->prov->fabric_attr, &ep->fab, NULL);
	if (ret) {
		print_err("fi_fabric returned %d", ret);
		goto free_info;
	}
	ret = fi_domain(ep->fab, ep->prov, &ep->dom, NULL);
	if (ret) {
		print_err("fi_domain returned %d", ret);
		goto close_fab;
	}

	qe = calloc(sizeof(struct qe), NVMF_DQ_DEPTH);
	if (!qe)
		return -ENOMEM;

	ep->qe = qe;

	for (i = 0; i < NVMF_DQ_DEPTH; i++) {
		qe[i].buf = alloc_buffer(ep, BUF_SIZE, &qe[i].recv_mr);
		if (!qe[i].buf)
			return ret;
	}

	return 0;
close_fab:
	fi_close(&ep->fab->fid);
	ep->fab = NULL;
free_info:
	fi_freeinfo(ep->prov);
	ep->prov = NULL;

	return ret;
}

static int init_endpoint(struct endpoint *ep, char *provider, char *node,
			 char *srvc)
{
	struct fi_info		*hints;
	int			 ret;

	ep->state = DISCONNECTED;

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

	ret = fi_getinfo(FI_VER, node, srvc, 0, hints, &ep->prov);

	free(hints->fabric_attr->prov_name);
	hints->fabric_attr->prov_name = NULL;

	fi_freeinfo(hints);

	if (ret) {
		print_err("fi_getinfo() returned %d", ret);
		return ret;
	}
	if (!ep->prov) {
		print_err("No matching provider found?");
		return -EINVAL;
	}

	return init_fabric(ep);
}

static int init_listener(struct listener *pep, char *provider, char *node,
			 char *srvc)
{
	struct fi_info		*hints;
	int			 ret;

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

	ret = fi_getinfo(FI_VER, node, srvc, FI_SOURCE, hints, &pep->prov);

	free(hints->fabric_attr->prov_name);
	hints->fabric_attr->prov_name = NULL;

	fi_freeinfo(hints);

	if (ret) {
		print_err("fi_getinfo() returned %d", ret);
		return ret;
	}
	if (!pep->prov) {
		print_err("No matching provider found?");
		return -EINVAL;
	}
	ret = fi_fabric(pep->prov->fabric_attr, &pep->fab, NULL);
	if (ret) {
		print_err("fi_fabric returned %d", ret);
		goto free_info;
	}
	ret = fi_domain(pep->fab, pep->prov, &pep->dom, NULL);
	if (ret) {
		print_err("fi_domain returned %d", ret);
		goto close_fab;
	}

	return 0;
close_fab:
	fi_close(&pep->fab->fid);
	pep->fab = NULL;
free_info:
	fi_freeinfo(pep->prov);
	pep->prov = NULL;

	return ret;
}

static int server_listen(struct listener *pep)
{
	struct fi_eq_attr	 eq_attr = { 0 };
	int			 ret;

	ret = fi_eq_open(pep->fab, &eq_attr, &pep->peq, NULL);
	if (ret) {
		print_err("fi_eq_open returned %d", ret);
		return ret;
	}
	ret = fi_passive_ep(pep->fab, pep->prov, &pep->pep, pep);
	if (ret) {
		print_err("fi_passive_ep returned %d", ret);
		return ret;
	}
	ret = fi_pep_bind(pep->pep, &pep->peq->fid, 0);
	if (ret) {
		print_err("fi_pep_bind returned %d", ret);
		return ret;
	}
	ret = fi_listen(pep->pep);
	if (ret) {
		print_err("fi_listen returned %d", ret);
		return ret;
	}

	return 0;
}

int pseudo_target_check_for_host(struct listener *pep, struct fi_info **info)
{
	struct fi_eq_cm_entry	 entry;
	uint32_t		 event;
	int			 ret;

	while (!stopped) {
		if (!pep->peq) {
			usleep(100);
			continue;
		}

		ret = fi_eq_sread(pep->peq, &event, &entry, sizeof(entry),
				  CTIMEOUT, 0);
		if (ret == sizeof(entry))
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			return ret;
		print_eq_error(pep->peq, ret);
		return ret;
	}

	if (stopped)
		return -ESHUTDOWN;

	if (event != FI_CONNREQ) {
		print_err("Unexpected CM event %d", event);
		return -FI_EOTHER;
	}

	*info = entry.info;

	return 0;
}

static int accept_connection(struct endpoint *ep)
{
	struct fi_eq_cm_entry	 entry;
	uint32_t		 event;
	int			 ret;
	int			 i;

	for (i = 0; i < NVMF_DQ_DEPTH; i++) {
		ret = fi_recv(ep->ep, ep->qe[i].buf, BUF_SIZE,
			      fi_mr_desc(ep->qe[i].recv_mr), 0, &ep->qe[i]);
		if (ret) {
			print_err("fi_recv returned %d", ret);
			return ret;
		}
	}

	ret = fi_accept(ep->ep, NULL, 0);
	if (ret) {
		print_err("fi_accept returned %d", ret);
		return ret;
	}

	ret = fi_eq_sread(ep->eq, &event, &entry, sizeof(entry),
			  CTIMEOUT, 0);
	if (ret != sizeof(entry)) {
		print_eq_error(ep->eq, ret);
		return ret;
	}

	if (event != FI_CONNECTED) {
		print_err("unexpected event %d", event);
		return -FI_EOTHER;
	}

	ep->state = CONNECTED;

	return 0;
}

static int create_endpoint(struct endpoint *ep, struct fi_info *info)
{
	struct fi_eq_attr	 eq_attr = { 0 };
	struct fi_cq_attr	 cq_attr = { 0 };
	int			 ret;

	info->ep_attr->tx_ctx_cnt = 1;
	info->ep_attr->rx_ctx_cnt = 2;
	info->tx_attr->iov_limit = 1;
	info->rx_attr->iov_limit = 1;
	info->tx_attr->inject_size = 0;

	cq_attr.size = NVMF_DQ_DEPTH;
	cq_attr.format = FI_CQ_FORMAT_MSG;
	cq_attr.wait_obj = FI_WAIT_UNSPEC;
	cq_attr.wait_cond = FI_CQ_COND_NONE;

	ret = fi_cq_open(ep->dom, &cq_attr, &ep->rcq, NULL);
	if (ret) {
		print_err("fi_cq_open for rcq returned %d", ret);
		return ret;
	}
	ret = fi_cq_open(ep->dom, &cq_attr, &ep->scq, NULL);
	if (ret) {
		print_err("fi_cq_open for scq returned %d", ret);
		return ret;
	}
	ret = fi_eq_open(ep->fab, &eq_attr, &ep->eq, NULL);
	if (ret) {
		print_err("fi_eq_open returned %d", ret);
		return ret;
	}
	ret = fi_endpoint(ep->dom, info, &ep->ep, ep);
	if (ret) {
		print_err("fi_endpoint returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ep->ep, &ep->rcq->fid, FI_RECV);
	if (ret) {
		print_err("fi_ep_bind for rcq returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ep->ep, &ep->scq->fid, FI_SEND);
	if (ret) {
		print_err("fi_ep_bind for scq returned %d", ret);
		return ret;
	}
	ret = fi_ep_bind(ep->ep, &ep->eq->fid, 0);
	if (ret) {
		print_err("fi_ep_bind for eq returned %d", ret);
		return ret;
	}
	ret = fi_enable(ep->ep);
	if (ret) {
		print_err("fi_enable returned %d", ret);
		return ret;
	}

	return 0;
}

static int client_connect(struct endpoint *ep)
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
		if (!ep->qe[i].recv_mr)
			return -ENOMEM;

		ret = fi_recv(ep->ep, ep->qe[i].buf, BUF_SIZE,
			      fi_mr_desc(ep->qe[i].recv_mr), 0, &ep->qe[i]);
		if (ret) {
			print_err("fi_recv returned %d", ret);
			return ret;
		}
	}

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

	ret = fi_connect(ep->ep, ep->prov->dest_addr, priv, bytes);
	if (ret) {
		print_err("fi_connect returned %d", ret);
		goto out;
	}

	while (!stopped) {
		ret = fi_eq_sread(ep->eq, &event, &entry, sizeof(entry),
				  IDLE_TIMEOUT, 0);
		if (ret == sizeof(entry))
			break;
		if (ret == -EAGAIN || ret == -EINTR)
			continue;
		print_eq_error(ep->eq, ret);
		goto out;
	}

	if (event != FI_CONNECTED) {
		print_err("fi_connect failed, event %d", event);
		ret = -ECONNRESET;
		goto out;
	}

	ep->state = CONNECTED;

	ret = 0;

out:
	free(priv);
	return ret;
}

static void cleanup_endpoint(struct endpoint *ep, int shutdown)
{
	int			 i;

	if (shutdown && (ep->state != DISCONNECTED))
		fi_shutdown(ep->ep, 0);

	if (ep->qe) {
		for (i = 0; i < NVMF_DQ_DEPTH; i++) {
			fi_close(&ep->qe[i].recv_mr->fid);
			free(ep->qe[i].buf);
		}
	}
	if (ep->data_mr) {
		fi_close(&ep->data_mr->fid);
		ep->data_mr = NULL;
	}
	if (ep->send_mr) {
		fi_close(&ep->send_mr->fid);
		ep->send_mr = NULL;
	}
	if (ep->ep) {
		fi_close(&ep->ep->fid);
		ep->ep = NULL;
	}
	if (ep->rcq) {
		fi_close(&ep->rcq->fid);
		ep->rcq = NULL;
	}
	if (ep->scq) {
		fi_close(&ep->scq->fid);
		ep->scq = NULL;
	}
	if (ep->eq) {
		fi_close(&ep->eq->fid);
		ep->eq = NULL;
	}
	if (ep->dom) {
		fi_close(&ep->dom->fid);
		ep->dom = NULL;
	}
	if (ep->info) {
		fi_freeinfo(ep->info);
		ep->info = NULL;
	}
	if (ep->prov) {
		fi_freeinfo(ep->prov);
		ep->prov = NULL;
	}
	if (ep->fab) {
		fi_close(&ep->fab->fid);
		ep->fab = NULL;
	}

	ep->state = DISCONNECTED;
}

void cleanup_listener(struct listener *pep)
{
	if (pep->pep) {
		fi_close(&pep->pep->fid);
		pep->pep = NULL;
	}
	if (pep->peq) {
		fi_close(&pep->peq->fid);
		pep->peq = NULL;
	}
	if (pep->dom) {
		fi_close(&pep->dom->fid);
		pep->dom = NULL;
	}
	if (pep->info) {
		fi_freeinfo(pep->info);
		pep->info = NULL;
	}
	if (pep->prov) {
		fi_freeinfo(pep->prov);
		pep->prov = NULL;
	}
	if (pep->fab) {
		fi_close(&pep->fab->fid);
		pep->fab = NULL;
	}
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
			//print_info("completed recv");
			//dump(qe->buf, comp.len);
			//memset(qe->buf, 0, BUF_SIZE);
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

int send_get_devices(struct endpoint *ep)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 bytes;

	bytes = sizeof(*cmd);

	data = (void *) &cmd[1];

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_admin_get_devices;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(4, sg->length);
	put_unaligned_le32(fi_mr_key(ep->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ep, cmd, bytes);
}

int send_set_port_config(struct endpoint *ep)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 bytes;

	bytes = sizeof(*cmd);

	data = (void *) &cmd[1];

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_admin_set_port_config;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(4, sg->length);
	put_unaligned_le32(fi_mr_key(ep->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ep, cmd, bytes);
}

int send_set_subsys_config(struct endpoint *ep)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 bytes;

	bytes = sizeof(*cmd);

	data = (void *) &cmd[1];

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_admin_set_subsys_config;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(4, sg->length);
	put_unaligned_le32(fi_mr_key(ep->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ep, cmd, bytes);
}

int send_get_subsys_usage(struct endpoint *ep)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	u64				*data;
	int				 bytes;

	bytes = sizeof(*cmd);

	data = (void *) &cmd[1];

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_admin_get_subsys_usage;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(4, sg->length);
	put_unaligned_le32(fi_mr_key(ep->send_mr), sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	return send_cmd(ep, cmd, bytes);
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
		print_err("fi_read failed");
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
		print_err("fi_write failed");
		print_cq_error(scq, ret);
		break;
	}

	return ret;
}

int send_msg_and_repost(struct endpoint *ep, struct qe *qe, void *msg, int len)
{
	struct fi_cq_err_entry comp;
	int ret;

	ret = fi_recv(ep->ep, qe->buf, BUF_SIZE, fi_mr_desc(qe->recv_mr),
		      0, qe);
	if (ret) {
		print_err("fi_recv returned %d", ret);
		return ret;
	}

	ret = fi_send(ep->ep, msg, len, fi_mr_desc(ep->send_mr),
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

	return 0;
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

int connect_target(struct endpoint *ep, char *type, char *node, char *port)
{
	struct nvme_command	*cmd;
	char			*provider;
	char			*verbs = "verbs";
	int			ret;

	if (!strcmp(type, "rdma"))
		provider = verbs;
	else
		return -EPROTONOSUPPORT;

	ret = init_endpoint(ep, provider, node, port);
	if (ret)
		return ret;

	ret = create_endpoint(ep, ep->prov);
	if (ret)
		return ret;

	ret = client_connect(ep);
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

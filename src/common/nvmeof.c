/*
 * NVMe over Fabrics Distributed Endpoint Management (NVMe-oF DEM).
 * Copyright (c) 2017-2018 Intel Corporation, Inc. All rights reserved.
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

#define NVME_CTRL_ENABLE 0x460001
#define NVME_CTRL_DISABLE 0x464001

#define NVME_DISC_KATO	360000 /* ms = minutes */

#define MSG_TIMEOUT     100

void dump(u8 *buf, int len)
{
	int			i, j, n = 0;
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

static int process_nvme_rsp(struct endpoint *ep)
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

	ret = rsp->status;

	ep->ops->repost_recv(ep->ep, qe);

	return ret;
}

static int send_fabric_connect(struct discovery_queue *dq)
{
	struct target			*target = dq->target;
	struct endpoint			*ep = &dq->ep;
	struct nvmf_connect_data	*data;
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	int				 bytes;
	int				 key;
	int				 ret;

	data = ep->data;
	key = ep->ops->remote_key(ep->data_mr);

	bytes = sizeof(*cmd);
	memset(cmd, 0, bytes);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->connect.opcode	= nvme_fabrics_command;
	cmd->connect.fctype	= nvme_fabrics_type_connect;
	cmd->connect.qid	= htole16(0);
	cmd->connect.sqsize	= htole16(NVMF_DQ_DEPTH);

	if (target->mgmt_mode == IN_BAND_MGMT)
		cmd->connect.kato = htole16(NVME_DISC_KATO);

	data->cntlid = htole16(NVME_CNTLID_DYNAMIC);
	strncpy(data->subsysnqn, NVME_DISC_SUBSYS_NAME, NVMF_NQN_SIZE);
	strncpy(data->hostnqn, dq->hostnqn, NVMF_NQN_SIZE);

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(sizeof(*data), sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		return ret;

	ret = process_nvme_rsp(ep);
	if (!ret || (target->mgmt_mode != IN_BAND_MGMT))
		return ret;

	print_err("misconfigured target %s, retry as locally managed",
		  target->alias);

	cmd->connect.kato = 0;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		return ret;

	return  process_nvme_rsp(ep);
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

	process_nvme_rsp(ep);

	return 0;
}

int send_get_nsdevs(struct endpoint *ep,
		    struct nvmf_ns_devices_rsp_page_hdr **hdr)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct xp_mr			*mr;
	u64				*data;
	int				 bytes;
	int				 key;
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

	cmd->fabrics.fctype	= nvme_fabrics_type_get_ns_devices;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(PAGE_SIZE, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	*hdr = (struct nvmf_ns_devices_rsp_page_hdr *) data;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		free(data);
	else
		ret =  process_nvme_rsp(ep);

	ep->ops->dealloc_key(mr);

	return ret;
}

int send_get_xports(struct endpoint *ep,
		    struct nvmf_transports_rsp_page_hdr **hdr)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct xp_mr			*mr;
	u64				*data;
	int				 bytes;
	int				 key;
	int				 ret;

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

	cmd->fabrics.fctype	= nvme_fabrics_type_get_transports;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) data;
	put_unaligned_le24(PAGE_SIZE, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	*hdr = (struct nvmf_transports_rsp_page_hdr *) data;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		free(data);
	else
		ret =  process_nvme_rsp(ep);

	ep->ops->dealloc_key(mr);

	return ret;
}

int send_set_port_config(struct endpoint *ep, int len,
			 struct nvmf_port_config_page_hdr *hdr)
{
	struct nvme_keyed_sgl_desc	*sg;
	struct nvme_command		*cmd = ep->cmd;
	struct xp_mr			*mr;
	int				 bytes;
	int				 key;
	int				 ret;

	if (!cmd)
		return -EINVAL;

	bytes = sizeof(*cmd);

	ret = ep->ops->alloc_key(ep->ep, hdr, len, &mr);
	if (ret)
		return ret;

	key = ep->ops->remote_key(mr);

	memset(cmd, 0, BUF_SIZE);

	cmd->common.flags	= NVME_CMD_SGL_METABUF;
	cmd->common.opcode	= nvme_fabrics_command;
	cmd->fabrics.fctype	= nvme_fabrics_type_set_port_config;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) hdr;
	put_unaligned_le24(len, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		return ret;

	ret = process_nvme_rsp(ep);

	ep->ops->dealloc_key(mr);

	return ret;
}

int send_set_subsys_config(struct endpoint *ep, int len,
			   struct nvmf_subsys_config_page_hdr *hdr)
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
	cmd->fabrics.fctype	= nvme_fabrics_type_set_subsys_config;

	sg = &cmd->common.dptr.ksgl;

	sg->addr = (u64) hdr;
	put_unaligned_le24(len, sg->length);
	put_unaligned_le32(key, sg->key);
	sg->type = NVME_KEY_SGL_FMT_DATA_DESC << 4;

	ret = send_cmd(ep, cmd, bytes);
	if (ret)
		return ret;

	ret = process_nvme_rsp(ep);

	ep->ops->dealloc_key(mr);

	return ret;
}

int send_get_subsys_usage(struct endpoint *ep, int len,
			  struct nvmf_subsys_usage_rsp_page_hdr *hdr)
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

	ret = process_nvme_rsp(ep);

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

	return process_nvme_rsp(ep);
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

	return process_nvme_rsp(ep);
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
		ret =  process_nvme_rsp(ep);

	ep->ops->dealloc_key(mr);

	return ret;
}


void disconnect_target(struct endpoint *ep, int shutdown)
{
	if (shutdown && (ep->state == CONNECTED))
		post_set_property(ep, NVME_REG_CC, NVME_CTRL_DISABLE);

	if (ep->mr)
		ep->ops->dealloc_key(ep->mr);

	ep->ops->destroy_endpoint(ep->ep);

	if (ep->qe)
		free(ep->qe);
	if (ep->cmd)
		free(ep->cmd);
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

int connect_target(struct discovery_queue *dq)
{
	struct portid		*portid = dq->portid;
	struct endpoint		*ep = &dq->ep;
	struct sockaddr		 dest = { 0 };
	struct sockaddr_in	*dest_in = (struct sockaddr_in *) &dest;
	struct sockaddr_in6	*dest_in6 = (struct sockaddr_in6 *) &dest;
	struct nvme_rdma_cm_req	*req;
	void			*cmd;
	void			*data;
	int			 bytes;
	int			 ret = 0;

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

	bytes = build_connect_data(&req, dq->hostnqn);

	ret = ep->ops->client_connect(ep->ep, &dest, req, bytes);

	if (bytes)
		free(req);
	if (ret)
		return ret;

	if (posix_memalign(&cmd, PAGE_SIZE, PAGE_SIZE))
		return -errno;

	memset(cmd, 0, PAGE_SIZE);

	ret = ep->ops->alloc_key(ep->ep, cmd, PAGE_SIZE, &ep->mr);
	if (ret)
		return ret;

	ep->cmd = cmd;

	if (posix_memalign(&data, PAGE_SIZE, PAGE_SIZE))
		return -errno;

	memset(data, 0, PAGE_SIZE);

	ret = ep->ops->alloc_key(ep->ep, data, PAGE_SIZE, &ep->data_mr);
	if (ret)
		return ret;

	ep->data = data;

	ret = send_fabric_connect(dq);
	if (ret)
		return ret;

	return send_set_property(ep, NVME_REG_CC, NVME_CTRL_ENABLE);
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

	ret = iface->ops->init_listener(&iface->listener,
					iface->pseudo_target_port);
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

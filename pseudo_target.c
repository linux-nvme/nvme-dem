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

#define _GNU_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cm.h>

#include "mongoose.h"
#include "common.h"
#include "incl/nvme.h"

#define IDLE_TIMEOUT 100
#define RETRY_COUNT  200

#define NVME_VER ((1 << 16) | (2 << 8) | 1) /* NVMe 1.2.1 */

static int handle_property_set(struct nvme_command *cmd, int *csts)
{
	int ret = 0;

	print_debug("nvme_fabrics_type_property_set %x = %llx",
		   cmd->prop_set.offset, cmd->prop_set.value);
	if (cmd->prop_set.offset == NVME_REG_CC)
		*csts = (cmd->prop_set.value == 0x460001) ? 1 : 8;
	else
		ret = -EINVAL;

	return ret;
}

static int handle_property_get(struct nvme_command *cmd,
			       struct nvme_completion *resp, int csts)
{
	int ret = 0;

	print_debug("nvme_fabrics_type_property_get %x", cmd->prop_get.offset);
	if (cmd->prop_get.offset == NVME_REG_CSTS)
		resp->result = htole32(csts);
	else if (cmd->prop_get.offset == NVME_REG_CAP)
		resp->result64 = htole64(0x200f0003ff);
	else if (cmd->prop_get.offset == NVME_REG_VS)
		resp->result = htole32(NVME_VER);
	else
		ret = -EINVAL;

	return ret;
}

static int handle_connect(struct endpoint *ep, u64 length, u64 addr, u64 key,
			  void *desc)
{
	struct nvmf_connect_data *data = (void *) ep->data;
	int ret;

	print_debug("nvme_fabrics_type_connect");

	ret = rma_read(ep->ep, ep->scq, data, length, desc, addr, key);
	if (ret) {
		print_err("rma_read returned %d", ret);
		return ret;
	}

	print_info("host '%s' connected", data->hostnqn);

	if (strcmp(data->subsysnqn, NVME_DISC_SUBSYS_NAME)) {
		print_err("bad subsystem '%s', expecting '%s'",
			  data->subsysnqn, NVME_DISC_SUBSYS_NAME);
		ret = -EINVAL;
	}
	if (data->cntlid != 0xffff) {
		print_err("bad controller id %x, expecting %x",
			  data->cntlid, 0xffff);
		ret = -EINVAL;
	}

	return ret;
}

static int handle_identify(struct endpoint *ep, struct nvme_command *cmd,
			   u64 length, u64 addr, u64 key, void *desc)
{
	struct nvme_id_ctrl *id = (void *) ep->data;
	int ret;

	if (htole32(cmd->identify.cns) != NVME_NQN_DISC) {
		print_err("unexpected identify command");
		return -EINVAL;
	}

	memset(id, 0, sizeof(*id));

	print_debug("identify");

	memset(id->fr, ' ', sizeof(id->fr));
	strncpy((char *)id->fr, " ", sizeof(id->fr));

	id->mdts = 0;
	id->cntlid = 0;
	id->ver = htole32(NVME_VER);
	id->lpa = (1 << 2);
	id->maxcmd = htole16(NVMF_DQ_DEPTH);
	id->sgls = htole32(1 << 0) | htole32(1 << 2) | htole32(1 << 20);

	strcpy(id->subnqn, NVME_DISC_SUBSYS_NAME);

	if (length > sizeof(*id))
		length = sizeof(*id);

	ret = rma_write(ep->ep, ep->scq, id, length, desc, addr, key);
	if (ret)
		print_err("rma_write returned %d", ret);

	return ret;
}

static int handle_get_log_page_count(struct endpoint *ep, u64 length, u64 addr,
				     u64 key, void *desc)
{
	struct nvmf_disc_rsp_page_hdr *log = (void *) ep->data;
	struct controller	*ctrl;
	struct subsystem	*subsys;
	int			 numrec = 0;
	int			 ret;

	print_debug("get_log_page_count");

	klist_for_each_entry(ctrl, ctrl_list, node)
		klist_for_each_entry(subsys, &ctrl->subsys_list, node)
			if (subsys->access)
				numrec++;

	log->numrec = numrec;
	log->genctr = 1;

	ret = rma_write(ep->ep, ep->scq, log, length, desc, addr, key);
	if (ret)
		print_err("rma_write returned %d", ret);

	return ret;
}

static int handle_get_log_pages(struct endpoint *ep, u64 length, u64 addr,
				u64 key, void *desc)
{
	struct nvmf_disc_rsp_page_hdr	*log = (void *) ep->data;
	struct nvmf_disc_rsp_page_entry *plogpage = (void *) &log[1];
	struct controller		*ctrl;
	struct subsystem		*subsys;
	int				 numrec = 0;
	int				 ret;

	print_debug("get_log_pages");

	klist_for_each_entry(ctrl, ctrl_list, node)
		klist_for_each_entry(subsys, &ctrl->subsys_list, node)
			if (subsys->access) {
				memcpy(plogpage, &subsys->log_page,
				       sizeof(*plogpage));
				numrec++;
				plogpage++;
			}

	log->numrec = numrec;
	log->genctr = 1;

	ret = rma_write(ep->ep, ep->scq, log, length, desc, addr, key);
	if (ret)
		print_err("rma_write returned %d", ret);

	return ret;
}

static void handle_request(struct endpoint *ep, struct qe *qe, int length)
{
	struct nvme_command	*cmd = (struct nvme_command *) qe->buf;
	struct nvmf_connect_command *c = &cmd->connect;
	struct nvme_completion	*resp = (void *) ep->cmd;
	int			 ret;
	u64			 len  = get_unaligned_le24(c->dptr.ksgl.length);
	void			*desc = fi_mr_desc(ep->data_mr);
	u64			 addr = c->dptr.ksgl.addr;
	u64			 key  = get_unaligned_le32(c->dptr.ksgl.key);

	memset(resp, 0, sizeof(*resp));

	resp->command_id = c->command_id;

#if DEBUG_REQUEST
	dump(qe->buf, length);
#else
	UNUSED(length);
#endif

	if (cmd->common.opcode == nvme_fabrics_command) {
		switch (cmd->fabrics.fctype) {
		case nvme_fabrics_type_property_set:
			ret = handle_property_set(cmd, &ep->csts);
			break;
		case nvme_fabrics_type_property_get:
			ret = handle_property_get(cmd, resp, ep->csts);
			break;
		case nvme_fabrics_type_connect:
			ret = handle_connect(ep, len, addr, key, desc);
			break;
		default:
			print_err("unknown fctype %d", cmd->fabrics.fctype);
			ret = -EINVAL;
		}
	} else if (cmd->common.opcode == nvme_admin_identify)
		ret = handle_identify(ep, cmd, len, addr, key, desc);
	else if (cmd->common.opcode == nvme_admin_get_log_page) {
		if (len == 16)
			ret = handle_get_log_page_count(ep, len, addr, key,
							desc);
		else
			ret = handle_get_log_pages(ep, len, addr, key, desc);
	} else {
		print_err("unknown nvme opcode %d\n", cmd->common.opcode);
		ret = -EINVAL;
	}

	if (ret)
		resp->status = NVME_SC_DNR;

	send_msg_and_repost(ep, qe, resp, sizeof(*resp));
}

static void *host_thread(void *arg)
{
	struct endpoint		*ep = arg;
	struct fi_cq_err_entry	 comp;
	struct fi_eq_cm_entry	 entry;
	uint32_t		 event;
	int			 retry_count = RETRY_COUNT;
	int			 ret;

	while (!stopped) {
		/* Listen and service Host requests */

		ret = fi_eq_read(ep->eq, &event, &entry, sizeof(entry), 0);
		if (ret == sizeof(entry))
			if (event == FI_SHUTDOWN)
				goto out;

		ret = fi_cq_sread(ep->rcq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			handle_request(ep, comp.op_context, comp.len);
		else if (ret == -EAGAIN) {
			retry_count--;
			if (!retry_count)
				goto out;
		} else if (ret != -EINTR) {
			print_cq_error(ep->rcq, ret);
			break;
		}
	}

out:
	cleanup_endpoint(ep);

	pthread_exit(NULL);

	return NULL;
}

static int run_pseudo_target_for_host(struct fi_info *prov)
{
	struct endpoint		*ep;
	pthread_attr_t		 pthread_attr;
	pthread_t		 pthread;
	int			 ret;

	ep = malloc(sizeof(*ep));
	if (!ep) {
		print_err("malloc failed");
		return -ENOMEM;
	}

	memset(ep, 0, sizeof(*ep));

	ep->prov = prov;

	ret = run_pseudo_target(ep);
	if (ret) {
		print_err("unable to accept host connect");
		print_err("run_pseudo_target returned %d", ret);
		goto err;
	}

	pthread_attr_init(&pthread_attr);
	ret = pthread_create(&pthread, &pthread_attr, host_thread, ep);
	if (ret) {
		print_err("failed to start host thread");
		goto err;
	}

	return 0;
err:
	cleanup_endpoint(ep);

	return ret;
}

static void refresh_log_pages(struct interface *iface)
{
	struct controller *ctrl;

	klist_for_each_entry(ctrl, ctrl_list, node) {
		if (!ctrl->refresh || (ctrl->iface != iface))
			continue;

		ctrl->refresh_countdown--;
		if (!ctrl->refresh_countdown) {
			fetch_log_pages(ctrl);
			ctrl->refresh_countdown = ctrl->refresh *
						  SECS / IDLE_TIMEOUT;
		}
	}
}

void init_controllers()
{
	struct controller	*ctrl;
	int			i;

	/* TODO: Untie host interfaces to target interfaces */

	for (i = 0; i < num_interfaces; i++)
		if (get_transport(&interfaces[i], json_ctx))
			print_err("Failed to get transport for iface %d", i);

	klist_for_each_entry(ctrl, ctrl_list, node) {
		fetch_log_pages(ctrl);
		ctrl->refresh_countdown = ctrl->refresh * SECS / IDLE_TIMEOUT;
	}
}

void *interface_thread(void *arg)
{
	struct interface	*iface = arg;
	struct listener		*listener = &iface->listener;
	struct fi_info		*info;
	int			 ret;

	ret = start_pseudo_target(listener, iface->trtype, iface->address,
				  iface->pseudo_target_port);
	if (ret) {
		print_err("Failed to start pseudo target");
		goto out;
	}

	while (!stopped) {
		ret = pseudo_target_check_for_host(listener, &info);
		if (ret == 0)
			ret = run_pseudo_target_for_host(info);
		else if (ret != -EAGAIN && ret != -EINTR)
			print_err("Host connection failed %d\n", ret);

		if (stopped)
			break;

		refresh_log_pages(iface);
	}

	cleanup_listener(listener);
out:
	num_interfaces--;

	pthread_exit(NULL);

	return NULL;
}

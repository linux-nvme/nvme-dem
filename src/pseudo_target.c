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

#define IDLE_TIMEOUT 100
#define RETRY_COUNT  200

#define NVME_VER ((1 << 16) | (2 << 8) | 1) /* NVMe 1.2.1 */

// #define DEBUG_COMMANDS

static int handle_property_set(struct nvme_command *cmd, int *csts)
{
	int			 ret = 0;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_type_property_set %x = %llx",
		   cmd->prop_set.offset, cmd->prop_set.value);
#endif
	if (cmd->prop_set.offset == NVME_REG_CC)
		*csts = (cmd->prop_set.value == 0x460001) ? 1 : 8;
	else
		ret = -EINVAL;

	return ret;
}

static int handle_property_get(struct nvme_command *cmd,
			       struct nvme_completion *resp, int csts)
{
	int			 ret = 0;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_type_property_get %x", cmd->prop_get.offset);
#endif
	if (cmd->prop_get.offset == NVME_REG_CSTS)
		resp->result = htole32(csts);
	else if (cmd->prop_get.offset == NVME_REG_CAP)
		resp->result64 = htole64(0x200f0003ffL);
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
	int			  ret;

	print_debug("nvme_fabrics_type_connect");

	ret = rma_read(ep->ep, ep->scq, data, length, desc, addr, key);
	if (ret) {
		print_err("rma_read returned %d", ret);
		return ret;
	}

	print_info("host '%s' connected", data->hostnqn);
	strncpy(ep->nqn, data->hostnqn, MAX_NQN_SIZE);

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
	struct nvme_id_ctrl	*id = (void *) ep->data;
	int			 ret;

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

static int host_access(struct subsystem *subsys, char *nqn)
{
	struct host		*entry;

	/* check if host is in subsys host_list it has non-zero access */
	/* return: 0 if no access; else access rights */

	list_for_each_entry(entry, &subsys->host_list, node)
		if (strcmp(entry->nqn, nqn) == 0)
			return entry->access;

	return subsys->access ? READ_WRITE : NONE;
}

static int handle_get_log_page_count(struct endpoint *ep, u64 length, u64 addr,
				     u64 key, void *desc)
{
	struct nvmf_disc_rsp_page_hdr	*log = (void *) ep->data;
	struct controller		*ctrl;
	struct subsystem		*subsys;
	int				 numrec = 0;
	int				 ret;

	print_debug("get_log_page_count");

	list_for_each_entry(ctrl, ctrl_list, node)
		list_for_each_entry(subsys, &ctrl->subsys_list, node) {
			if (!subsys->log_page_valid)
				continue;
			if (host_access(subsys, ep->nqn))
				numrec++;
		}

	log->numrec = numrec;
	log->genctr = 1;

	ret = rma_write(ep->ep, ep->scq, log, length, desc, addr, key);
	if (ret)
		print_err("rma_write returned %d", ret);

	return ret;
}

static int handle_get_log_pages(struct endpoint *ep, u64 len, u64 addr,
				u64 key)
{
	struct nvmf_disc_rsp_page_hdr	*log;
	struct nvmf_disc_rsp_page_entry *plogpage;
	struct controller		*ctrl;
	struct subsystem		*subsys;
	struct fid_mr			*mr;
	int				 numrec = 0;
	int				 ret;

	print_debug("get_log_pages");

	log = alloc_buffer(ep, len, &mr);
	if (!log)
		return -ENOMEM;

	plogpage = (void *) (&log[1]);

	list_for_each_entry(ctrl, ctrl_list, node)
		list_for_each_entry(subsys, &ctrl->subsys_list, node) {
			if (!subsys->log_page_valid)
				continue;
			if (host_access(subsys, ep->nqn)) {
				memcpy(plogpage, &subsys->log_page,
				       sizeof(*plogpage));
				numrec++;
				plogpage++;
			}
		}

	log->numrec = numrec;
	log->genctr = 1;

	ret = rma_write(ep->ep, ep->scq, log, len, fi_mr_desc(mr), addr, key);
	if (ret)
		print_err("rma_write returned %d", ret);

	fi_close(&mr->fid);
	free(log);

	return ret;
}

static void handle_request(struct endpoint *ep, struct qe *qe, int length)
{
	struct nvme_command	*cmd = (struct nvme_command *) qe->buf;
	struct nvme_completion	*resp = (void *) ep->cmd;
	struct nvmf_connect_command *c = &cmd->connect;
	u64			 len  = get_unaligned_le24(c->dptr.ksgl.length);
	void			*desc = fi_mr_desc(ep->data_mr);
	u64			 addr = c->dptr.ksgl.addr;
	u64			 key  = get_unaligned_le32(c->dptr.ksgl.key);
	int			 ret;

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
			ret = handle_get_log_pages(ep, len, addr, key);
	} else {
		print_err("unknown nvme opcode %d\n", cmd->common.opcode);
		ret = -EINVAL;
	}

	if (ret)
		resp->status = NVME_SC_DNR;

	send_msg_and_repost(ep, qe, resp, sizeof(*resp));
}

#define HOST_QUEUE_MAX 3 /* min of 3 otherwise cannot tell if full */
struct host_queue {
	struct endpoint         *ep[HOST_QUEUE_MAX];
	int			tail, head;
};

static inline int is_empty(struct host_queue *q)
{
        return q->head == q->tail;
}
static inline int is_full(struct host_queue *q)
{
        return ((q->head + 1) % HOST_QUEUE_MAX) == q->tail;
}
#ifdef DEBUG_HOST_QUEUE
static inline void dump_queue(struct host_queue *q)
{
	print_debug("ep { %p, %p, %p }, tail %d, head %d",
		    q->ep[0], q->ep[1], q->ep[2], q->tail, q->head);
}
#endif
static inline int add_one(struct host_queue *q, struct endpoint *ep)
{
        if (is_full(q))
                return -1;

        q->ep[q->head] = ep;
        q->head = (q->head + 1) % HOST_QUEUE_MAX;
#ifdef DEBUG_HOST_QUEUE
	dump_queue(q);
#endif
        return 0;
}
static inline int take_one(struct host_queue *q, struct endpoint **ep)
{
        if (is_empty(q)) 
                return -1;

	if (!q->ep[q->tail])
		return 1;

        *ep = q->ep[q->tail];
        q->ep[q->tail] = NULL;
        q->tail = (q->tail + 1) % HOST_QUEUE_MAX;
#ifdef DEBUG_HOST_QUEUE
	dump_queue(q);
#endif
        return 0;
}

static void *host_thread(void *arg)
{
	struct host_queue	*q = arg;
	struct endpoint		*ep = NULL;
	struct fi_cq_err_entry	 comp;
	struct fi_eq_cm_entry	 entry;
	uint32_t		 event = 0;
	int			 retry_count = RETRY_COUNT;
	int			 ret;
wait:
	do {
		usleep(100);
		ret = take_one(q, &ep);
	} while (!!ret && !stopped);

	/* Service Host requests */
	while (!stopped) {
		ret = fi_eq_read(ep->eq, &event, &entry, sizeof(entry), 0);
		if (ret == sizeof(entry))
			if (event == FI_SHUTDOWN)
				goto out;

		ret = fi_cq_sread(ep->rcq, &comp, 1, NULL, IDLE_TIMEOUT);
		if (ret > 0)
			handle_request(ep, comp.op_context, comp.len);
		else if (ret == -EAGAIN) {
			retry_count--;
			if (retry_count <= 0)
				goto out;
		} else if (ret != -EINTR) {
			print_cq_error(ep->rcq, ret);
			break;
		}
	}
out:
	if (ep) {
		disconnect_controller(ep, event != FI_SHUTDOWN);
		free(ep);
		ep = NULL;
	}

	if (stopped != 1)
		goto wait;

	while (!is_empty(q))
		if (!take_one(q, &ep)) {
			disconnect_controller(ep, 1);
			free(ep);
		}

	pthread_exit(NULL);

	return NULL;
}

static int add_host_to_queue(struct fi_info *prov, struct host_queue *q)
{
	struct endpoint		*ep;
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
		goto err1;
	}

	while (is_full(q) && !stopped)
		usleep(100);

	add_one(q, ep);

	usleep(100);

	return 0;
err1:
	free(ep);
	return ret;
}

void init_controllers(void)
{
	struct controller	*ctrl;

	if (build_ctrl_list(json_ctx)) {
		print_err("Failed to build controller list");
		return;
	}

	list_for_each_entry(ctrl, ctrl_list, node) {
		fetch_log_pages(ctrl);
		ctrl->refresh_countdown =
			ctrl->refresh * MINUTES / IDLE_TIMEOUT;
	}
}

void *interface_thread(void *arg)
{
	struct interface	*iface = arg;
	struct listener		*listener = &iface->listener;
	struct fi_info		*info;
	struct host_queue	 q;
	pthread_attr_t		 pthread_attr;
	pthread_t		 pthread;
	int			 ret;

	ret = start_pseudo_target(listener, iface->trtype, iface->address,
				  iface->pseudo_target_port);
	if (ret) {
		print_err("Failed to start pseudo target");
		goto out1;
	}

	signal(SIGTERM, SIG_IGN);

	memset(&q, 0, sizeof(q));

	pthread_attr_init(&pthread_attr);

	ret = pthread_create(&pthread, &pthread_attr, host_thread, &q);
	if (ret) {
		print_err("failed to start host thread");
		goto out2;
	}

	while (!stopped) {
		ret = pseudo_target_check_for_host(listener, &info);
		if (ret == 0)
			ret = add_host_to_queue(info, &q);
		else if (ret != -EAGAIN && ret != -EINTR)
			print_err("Host connection failed %d\n", ret);
	}

	pthread_join(pthread, NULL);

out2:
	cleanup_listener(listener);
out1:
	num_interfaces--;

	pthread_exit(NULL);

	return NULL;
}

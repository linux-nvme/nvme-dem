// SPDX-License-Identifier: DUAL GPL-2.0/BSD
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

#define _GNU_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "mongoose.h"
#include "common.h"
#include "ops.h"

#define RETRY_COUNT	1200	// 2 min since multiplier of delay timeout
#define DELAY_TIMEOUT	100	// ms
#define KATO_INTERVAL	500	// ms per spec

#define NVME_VER ((1 << 16) | (2 << 8) | 1) /* NVMe 1.2.1 */

#define DEBUG_COMMANDS // TODO comment this out

struct host_conn {
	struct list_head	 node;
	struct endpoint		*ep;
	struct timeval		 timeval;
	int			 countdown;
	int			 kato;
};

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
		ret = NVME_SC_INVALID_FIELD;

	return ret;
}

static int handle_property_get(struct nvme_command *cmd,
			       struct nvme_completion *resp, int csts)
{
	u64			 value;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_type_property_get %x", cmd->prop_get.offset);
#endif
	if (cmd->prop_get.offset == NVME_REG_CSTS)
		value = csts;
	else if (cmd->prop_get.offset == NVME_REG_CAP)
		value = 0x200f0003ffL;
	else if (cmd->prop_get.offset == NVME_REG_VS)
		value = NVME_VER;
	else
		return NVME_SC_INVALID_FIELD;

	resp->result.U64 = htole64(value);

	return 0;
}

static int handle_set_features(struct nvme_command *cmd, u32 *kato)
{
	u32			 cdw10 = ntohl(cmd->common.cdw10[0]);
	int			 ret;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_type_set_features");
#endif

	if ((cdw10 & 0xff) == *kato) {
		*kato = ntohl(cmd->common.cdw10[1]);
		ret = 0;
	} else
		ret = NVME_SC_FEATURE_NOT_CHANGEABLE;

	return ret;
}

static int handle_connect(struct endpoint *ep, u64 addr, u64 key, u64 len)
{
	struct nvmf_connect_data *data = ep->data;
	int			 ret;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_connect");
#endif

	ret = ep->ops->rma_read(ep->ep, ep->data, addr, len, key, ep->data_mr);
	if (ret) {
		print_err("rma_read returned %d", ret);
		goto out;
	}

	print_info("host '%s' connected", data->hostnqn);
	strncpy(ep->nqn, data->hostnqn, MAX_NQN_SIZE);

	if (strcmp(data->subsysnqn, NVME_DISC_SUBSYS_NAME) &&
	    strcmp(data->subsysnqn, NVME_DOMAIN_SUBSYS_NAME)) {
		print_err("bad subsystem '%s', expecting '%s' or '%s'",
			  data->subsysnqn, NVME_DISC_SUBSYS_NAME,
			  NVME_DOMAIN_SUBSYS_NAME);
		ret = NVME_SC_CONNECT_INVALID_HOST;
	}

	if (data->cntlid != 0xffff) {
		print_err("bad controller id %x, expecting %x",
			  data->cntlid, 0xffff);
		ret = NVME_SC_CONNECT_INVALID_PARAM;
	}
out:
	return ret;
}

static int handle_identify(struct endpoint *ep, struct nvme_command *cmd,
			   u64 addr, u64 key, u64 len)
{
	struct nvme_id_ctrl	*id = ep->data;
	int			 ret;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_identify");
#endif

	if (htole32(cmd->identify.cns) != NVME_ID_CNS_CTRL) {
		print_err("unexpected identify command");
		return NVME_SC_BAD_ATTRIBUTES;
	}

	memset(id, 0, sizeof(*id));

	memset(id->fr, ' ', sizeof(id->fr));
	strncpy((char *) id->fr, " ", sizeof(id->fr));

	id->mdts = 0;
	id->cntlid = 0;
	id->ver = htole32(NVME_VER);
	id->lpa = (1 << 2);
	id->maxcmd = htole16(NVMF_DQ_DEPTH);
	id->sgls = htole32(1 << 0) | htole32(1 << 2) | htole32(1 << 20);

	strcpy(id->subnqn, NVME_DISC_SUBSYS_NAME);

	if (len > sizeof(*id))
		len = sizeof(*id);

	ret = ep->ops->rma_write(ep->ep, ep->data, addr, len, key, ep->data_mr);
	if (ret) {
		print_err("rma_write returned %d", ret);
		ret = NVME_SC_WRITE_FAULT;
	}

	return ret;
}

static int get_nsdev(void *data)
{
	struct nvmf_get_ns_devices_hdr *hdr = data;
	struct nvmf_get_ns_devices_entry *entry;
	struct nsdev		*dev;
	int			 cnt = 0;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_get_ns_devices");
#endif

	entry = (struct nvmf_get_ns_devices_entry *) &hdr->data;

	list_for_each_entry(dev, devices, node) {
		memset(entry, 0, sizeof(*entry));
		entry->devid = dev->devid;
		entry->nsid = dev->nsid;
		cnt++;
		entry++;
	}

	hdr->num_entries = cnt;

	return cnt * sizeof(*entry) + sizeof(*hdr) - 1;
}

static int get_xport(void *data)
{
	struct nvmf_get_transports_hdr *hdr = data;
	struct nvmf_get_transports_entry *entry;
	struct portid		*xport;
	int			 cnt = 0;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_get_transports");
#endif

	entry = (struct nvmf_get_transports_entry *) &hdr->data;

	list_for_each_entry(xport, interfaces, node) {
		memset(entry, 0, sizeof(*entry));
		entry->trtype = to_trtype(xport->type);
		entry->adrfam = to_adrfam(xport->family);
		strcpy(entry->traddr, xport->address);
		cnt++;
		entry++;
	}

	hdr->num_entries = cnt;

	return cnt * sizeof(*entry) + sizeof(*hdr) - 1;
}

static inline int set_portid(void *data)
{
	struct nvmf_port_config_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_port_config_entry *) data;

	return create_portid(entry->portid, (char *) adrfam_str(entry->adrfam),
			     (char *) trtype_str(entry->trtype), entry->treq,
			     entry->traddr, atoi(entry->trsvcid));
}

static inline int del_portid(void *data)
{
	struct nvmf_port_delete_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_port_delete_entry *) data;

	return delete_portid(entry->portid);
}

static inline int set_subsys(void *data)
{
	struct nvmf_subsys_config_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_subsys_config_entry *) data;

	return create_subsys(entry->subnqn, entry->allowanyhost);
}

static inline int del_subsys(void *data)
{
	struct nvmf_subsys_delete_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_subsys_delete_entry *) data;

	return delete_subsys(entry->subnqn);
}

static inline int set_ns(void *data)
{
	struct nvmf_ns_config_entry *entry;
	int			 devid;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_ns_config_entry *) data;

	if (entry->deviceid == NVMF_NULLB_DEVID)
		devid = NULLB_DEVID;
	else
		devid = entry->deviceid;

	return create_ns(entry->subnqn, entry->nsid, devid, entry->devicensid);
}

static inline int del_ns(void *data)
{
	struct nvmf_ns_delete_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_ns_delete_entry *) data;

	return delete_ns(entry->subnqn, entry->nsid);
}

static inline int set_host(void *data)
{
	struct nvmf_host_config_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_host_config_entry *) data;

	return create_host(entry->hostnqn);
}

static inline int del_host(void *data)
{
	struct nvmf_host_delete_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_host_delete_entry *) data;

	return delete_host(entry->hostnqn);
}

static inline int link_portid(void *data)
{
	struct nvmf_link_port_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_link_port_entry *) data;

	return link_port_to_subsys(entry->subnqn, entry->portid);
}

static inline int unlink_portid(void *data)
{
	struct nvmf_link_port_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_link_port_entry *) data;

	return unlink_port_from_subsys(entry->subnqn, entry->portid);
}

static inline int link_host(void *data)
{
	struct nvmf_link_host_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_link_host_entry *) data;

	return link_host_to_subsys(entry->subnqn, entry->hostnqn);
}

static inline int unlink_host(void *data)
{
	struct nvmf_link_host_entry *entry;

#ifdef DEBUG_COMMANDS
	print_debug("nvme_fabrics_set_config - %s", __func__);
#endif

	entry = (struct nvmf_link_host_entry *) data;

	return unlink_host_from_subsys(entry->subnqn, entry->hostnqn);
}

static int handle_get_config(struct nvme_command *cmd, struct endpoint *ep,
			     u64 addr, u64 key, u64 len)
{
	struct nvmf_resource_config_command *c = &cmd->config;
	int			  ret;

	switch (c->command_id) {
	case nvmf_get_ns_config:
		len = get_nsdev(ep->data);
		break;
	case nvmf_get_xport_config:
		len = get_xport(ep->data);
		break;
	default:
		print_err("Unknown set config id %x", c->command_id);
	}

	ret = ep->ops->rma_write(ep->ep, ep->data, addr, len, key, ep->data_mr);
	if (ret) {
		print_err("rma_write returned %d", ret);
		ret = NVME_SC_WRITE_FAULT;
	}

	return ret;
}

static int handle_reset_config(struct nvme_command *cmd)
{
	struct nvmf_resource_config_command *c = &cmd->config;
	int			  ret;

	switch (c->command_id) {
	case nvmf_reset_config:
		reset_config();
		ret = 0;
		break;
	default:
		print_err("Unknown set command id %x", c->command_id);
		ret = NVME_SC_INVALID_FIELD;
	}

	return ret;
}

static int handle_set_config(struct nvme_command *cmd, struct endpoint *ep,
			     u64 addr, u64 key, u64 len)
{
	struct nvmf_resource_config_command *c = &cmd->config;
	int			  ret;

	ret = ep->ops->rma_read(ep->ep, ep->data, addr, len, key, ep->data_mr);
	if (ret) {
		print_err("rma_read returned %d", ret);
		goto out;
	}

	switch (c->command_id) {
	case nvmf_set_port_config:
		ret = set_portid(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_del_port_config:
		ret = del_portid(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_link_port_config:
		ret = link_portid(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_unlink_port_config:
		ret = unlink_portid(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_set_subsys_config:
		ret = set_subsys(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_del_subsys_config:
		ret = del_subsys(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_set_ns_config:
		ret = set_ns(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_del_ns_config:
		ret = del_ns(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_set_host_config:
		ret = set_host(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_del_host_config:
		ret = del_host(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_link_host_config:
		ret = link_host(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	case nvmf_unlink_host_config:
		ret = unlink_host(ep->data);
		if (ret)
			ret = NVME_SC_ACCESS_DENIED;
		break;
	default:
		print_err("Unknown set config id %x", c->command_id);
		ret = NVME_SC_INVALID_FIELD;
	}

out:
	return ret;
}

static int handle_request(struct host_conn *host, struct qe *qe, void *buf,
			  int length)
{
	struct endpoint			*ep = host->ep;
	struct nvme_command		*cmd = (struct nvme_command *) buf;
	struct nvme_completion		*resp = (void *) ep->cmd;
	struct nvmf_connect_command	*c = &cmd->connect;
	u64				 addr;
	u32				 len;
	u32				 key;
	u32				 kato;
	int				 ret;

	addr	= c->dptr.ksgl.addr;
	len	= get_unaligned_le24(c->dptr.ksgl.length);
	key	= get_unaligned_le32(c->dptr.ksgl.key);

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
			ret = handle_connect(ep, addr, key, len);
			break;
		case nvme_fabrics_type_resource_config_get:
			ret = handle_get_config(cmd, ep, addr, key, len);
			break;
		case nvme_fabrics_type_resource_config_set:
			ret = handle_set_config(cmd, ep, addr, key, len);
			break;
		case nvme_fabrics_type_resource_config_reset:
			ret = handle_reset_config(cmd);
			break;
		default:
			print_err("unknown fctype %d", cmd->fabrics.fctype);
			ret = NVME_SC_INVALID_OPCODE;
		}
	} else if (cmd->common.opcode == nvme_admin_identify)
		ret = handle_identify(ep, cmd, addr, key, len);
	else if (cmd->common.opcode == nvme_admin_keep_alive)
		ret = 0;
	else if (cmd->common.opcode == nvme_admin_set_features) {
		ret = handle_set_features(cmd, &kato);
		if (ret)
			ret = NVME_SC_INVALID_FIELD;
		else
			host->kato = kato * (KATO_INTERVAL / DELAY_TIMEOUT);
	} else {
		print_err("unknown nvme opcode %d", cmd->common.opcode);
		ret = NVME_SC_INVALID_OPCODE;
	}

	if (ret)
		resp->status = (NVME_SC_DNR | ret) << 1;

	ep->ops->send_msg(ep->ep, resp, sizeof(*resp), ep->mr);
	ep->ops->repost_recv(ep->ep, qe->qe);

	return ret;
}

#define HOST_QUEUE_MAX 3 /* min of 3 otherwise cannot tell if full */
struct host_queue {
	struct endpoint		*ep[HOST_QUEUE_MAX];
	int			 tail, head;
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

static inline int add_new_host_conn(struct host_queue *q, struct endpoint *ep)
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

static inline int get_new_host_conn(struct host_queue *q, struct endpoint **ep)
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
	struct qe		 qe;
	struct timeval		 timeval;
	struct list_head	 host_list;
	struct host_conn	*next;
	struct host_conn	*host;
	void			*buf;
	int			 len;
	int			 delta;
	int			 ret;

	INIT_LIST_HEAD(&host_list);

	while (!stopped) {
		gettimeofday(&timeval, NULL);

		do {
			ret = get_new_host_conn(q, &ep);

			if (!ret) {
				host = malloc(sizeof(*host));
				if (!host)
					goto out;

				host->ep	= ep;
				host->kato	= RETRY_COUNT;
				host->countdown	= RETRY_COUNT;
				host->timeval	= timeval;

				list_add_tail(&host->node, &host_list);
			}
		} while (!ret && !stopped);

		/* Service Host requests */
		list_for_each_entry_safe(host, next, &host_list, node) {
			ep = host->ep;
loop:
			ret = ep->ops->poll_for_msg(ep->ep, &qe.qe, &buf, &len);
			if (!ret) {
				ret = handle_request(host, &qe, buf, len);
				if (!ret) {
					host->countdown	= host->kato;
					host->timeval	= timeval;
					goto loop;
				}
			}

			if (ret == -EAGAIN)
				if (--host->countdown > 0)
					continue;

			disconnect_endpoint(ep, !stopped);

			print_info("host '%s' disconnected", ep->nqn);

			free(ep);
			list_del(&host->node);
			free(host);
		}

		delta = msec_delta(timeval);
		if (delta < DELAY_TIMEOUT)
			usleep((DELAY_TIMEOUT - delta) * 1000);
	}
out:
	list_for_each_entry_safe(host, next, &host_list, node) {
		disconnect_endpoint(host->ep, 1);
		free(host->ep);
		free(host);
	}

	while (!is_empty(q))
		if (!get_new_host_conn(q, &ep)) {
			disconnect_endpoint(ep, 1);
			free(ep);
		}

	pthread_exit(NULL);

	return NULL;
}

static int add_host_to_queue(void *id, struct xp_ops *ops, struct host_queue *q)
{
	struct endpoint		*ep;
	int			 ret;

	ep = malloc(sizeof(*ep));
	if (!ep) {
		print_err("malloc failed");
		return -ENOMEM;
	}

	memset(ep, 0, sizeof(*ep));

	ep->ops = ops;

	ret = run_pseudo_target(ep, id);
	if (ret) {
		print_err("run_pseudo_target returned %d", ret);
		goto out;
	}

	while (is_full(q) && !stopped)
		usleep(100);

	add_new_host_conn(q, ep);

	usleep(20);

	return 0;
out:
	free(ep);
	return ret;
}

void *interface_thread(void *arg)
{
	struct host_iface	*iface = arg;
	struct xp_pep		*listener;
	void			*id;
	struct host_queue	 q;
	pthread_attr_t		 pthread_attr;
	pthread_t		 pthread;
	int			 ret;

	ret = start_pseudo_target(iface);
	if (ret) {
		print_err("Failed to start pseudo target");
		goto out1;
	}

	listener = iface->listener;

	signal(SIGTERM, SIG_IGN);

	memset(&q, 0, sizeof(q));

	pthread_attr_init(&pthread_attr);

	ret = pthread_create(&pthread, &pthread_attr, host_thread, &q);
	if (ret) {
		print_err("failed to start host thread");
		goto out2;
	}

	while (!stopped) {
		ret = iface->ops->wait_for_connection(listener, &id);
		if (ret == 0)
			add_host_to_queue(id, iface->ops, &q);
		else if (ret == -ESHUTDOWN || ret != -EAGAIN)
			continue;
		else if (ret == -ECONNRESET)
			continue;
		else
			print_err("Host connection failed %d", ret);
	}

	pthread_join(pthread, NULL);
out2:
	iface->ops->destroy_listener(listener);
out1:
	pthread_exit(NULL);

	return NULL;
}

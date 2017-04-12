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

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "common.h"

static const char *arg_str(const char * const *strings, size_t array_size,
			   size_t idx)
{
	if (idx < array_size && strings[idx])
		return strings[idx];
	return "unrecognized";
}

static const char * const trtypes[] = {
	[NVMF_TRTYPE_RDMA]	= "rdma",
	[NVMF_TRTYPE_FC]	= "fibre-channel",
	[NVMF_TRTYPE_LOOP]	= "loop",
};

static const char *trtype_str(u8 trtype)
{
	return arg_str(trtypes, ARRAY_SIZE(trtypes), trtype);
}

static const char * const adrfams[] = {
	[NVMF_ADDR_FAMILY_PCI]	= "pci",
	[NVMF_ADDR_FAMILY_IP4]	= "ipv4",
	[NVMF_ADDR_FAMILY_IP6]	= "ipv6",
	[NVMF_ADDR_FAMILY_IB]	= "infiniband",
	[NVMF_ADDR_FAMILY_FC]	= "fibre-channel",
};

static inline const char *adrfam_str(u8 adrfam)
{
	return arg_str(adrfams, ARRAY_SIZE(adrfams), adrfam);
}

static const char * const subtypes[] = {
	[NVME_NQN_DISC]		= "discovery subsystem",
	[NVME_NQN_NVME]		= "nvme subsystem",
};

static inline const char *subtype_str(u8 subtype)
{
	return arg_str(subtypes, ARRAY_SIZE(subtypes), subtype);
}

static const char * const treqs[] = {
	[NVMF_TREQ_NOT_SPECIFIED]	= "not specified",
	[NVMF_TREQ_REQUIRED]		= "required",
	[NVMF_TREQ_NOT_REQUIRED]	= "not required",
};

static inline const char *treq_str(u8 treq)
{
	return arg_str(treqs, ARRAY_SIZE(treqs), treq);
}

static const char * const prtypes[] = {
	[NVMF_RDMA_PRTYPE_NOT_SPECIFIED]	= "not specified",
	[NVMF_RDMA_PRTYPE_IB]			= "infiniband",
	[NVMF_RDMA_PRTYPE_ROCE]			= "roce",
	[NVMF_RDMA_PRTYPE_ROCEV2]		= "roce-v2",
	[NVMF_RDMA_PRTYPE_IWARP]		= "iwarp",
};

static inline const char *prtype_str(u8 prtype)
{
	return arg_str(prtypes, ARRAY_SIZE(prtypes), prtype);
}

static const char * const qptypes[] = {
	[NVMF_RDMA_QPTYPE_CONNECTED]	= "connected",
	[NVMF_RDMA_QPTYPE_DATAGRAM]	= "datagram",
};

static inline const char *qptype_str(u8 qptype)
{
	return arg_str(qptypes, ARRAY_SIZE(qptypes), qptype);	}

static const char * const cms[] = {
	[NVMF_RDMA_CMS_RDMA_CM] = "rdma-cm",
};

static const char *cms_str(u8 cm)
{
	return arg_str(cms, ARRAY_SIZE(cms), cm);
}

static int get_logpages(struct controller *ctrl,
			struct nvmf_disc_rsp_page_hdr **logp, u32 *numrec)
{
	struct nvmf_disc_rsp_page_hdr	*log;
	unsigned int			 log_size = 0;
	unsigned long			 genctr;
	int				 ret = 0;
	size_t				 offset;

	ret = connect_controller(&ctrl->ctx, ctrl->trtype,
				 ctrl->address, ctrl->port);
	if (ret)
		return ret;

	offset = offsetof(struct nvmf_disc_rsp_page_hdr, numrec);
	log_size = offset + sizeof(log->numrec);
	log_size = round_up(log_size, sizeof(u32));

	ret = send_get_log_page(&ctrl->ctx, log_size, &log);
	if (ret) {
		print_err("Failed to fetch number of discovery log entries");
		ret = -ENODATA;
		goto err;
	}

	genctr = le64toh(log->genctr);
	*numrec = le32toh(log->numrec);

	free(log);

	if (*numrec == 0) {
		print_err("No discovery log on controller %s", ctrl->address);
		ret = -ENODATA;
		goto err;
	}

#ifdef DEBUG_LOG_PAGES
	print_debug("number of records to fetch is %d", *numrec);
#endif

	log_size = sizeof(struct nvmf_disc_rsp_page_hdr) +
		sizeof(struct nvmf_disc_rsp_page_entry) * *numrec;

	ret = send_get_log_page(&ctrl->ctx, log_size, &log);
	if (ret) {
		print_err("Failed to fetch discovery log entries");
		ret = -ENODATA;
		goto err;
	}

	if ((*numrec != le32toh(log->numrec)) ||
	    ( genctr != le64toh(log->genctr))) {
		print_err("# records for last two get log pages not equal");
		ret = -EINVAL;
		goto err;
	}

	*logp = log;

err:
	disconnect_controller(&ctrl->ctx);
	return ret;
}

static void print_discovery_log(struct nvmf_disc_rsp_page_hdr *log, int numrec)
{
#ifdef DEBUG_LOG_PAGES
	int i;

	print_debug("Discovery Log Number of Records %d, "
		    "Generation counter %"PRIu64"",
		    numrec, (uint64_t)le64toh(log->genctr));

	for (i = 0; i < numrec; i++) {
		struct nvmf_disc_rsp_page_entry *e = &log->entries[i];

		print_debug("=====Discovery Log Entry %d======", i);
		print_debug("trtype:  %s", trtype_str(e->trtype));
		print_debug("adrfam:  %s", adrfam_str(e->adrfam));
		print_debug("subtype: %s", subtype_str(e->subtype));
		print_debug("treq:    %s", treq_str(e->treq));
		print_debug("portid:  %d", e->portid);
		print_debug("trsvcid: %s", e->trsvcid);
		print_debug("subnqn:  %s", e->subnqn);
		print_debug("traddr:  %s", e->traddr);

		switch (e->trtype) {
		case NVMF_TRTYPE_RDMA:
			print_debug("rdma_prtype: %s",
				    prtype_str(e->tsas.rdma.prtype));
			print_debug("rdma_qptype: %s",
				    qptype_str(e->tsas.rdma.qptype));
			print_debug("rdma_cms:    %s",
				    cms_str(e->tsas.rdma.cms));
			print_debug("rdma_pkey: 0x%04x",
				    e->tsas.rdma.pkey);
			break;
		}
	}
#else
	UNUSED(log);
	UNUSED(numrec);
#endif
}

static void save_log_pages(struct nvmf_disc_rsp_page_hdr *log, int numrec,
			   struct controller *ctrl)
{
	int			i;
	struct subsystem	*subsys;
	struct nvmf_disc_rsp_page_entry *e;

	for (i = 0; i < numrec; i++) {
		e = &log->entries[i];
		subsys = ctrl->subsystem_list;
		while (subsys) {
			if ((strcmp(subsys->nqn, e->subnqn) == 0)) {
				subsys->log_page = *e;
				break;
			}
			subsys = subsys->next;
		}
		if (!subsys)
			print_err("subsystem for log page (%s) not found",
				  e->subnqn);
	}
}

void fetch_log_pages(struct controller *ctrl)
{
	struct nvmf_disc_rsp_page_hdr	*log = NULL;
	u32				 num_records = 0;

	if (get_logpages(ctrl, &log, &num_records)) {
		print_err("Failed to get logpage for controller %s",
			  ctrl->address);
		return;
	}

	save_log_pages(log, num_records, ctrl);

	print_discovery_log(log, num_records);

	free(log);
}

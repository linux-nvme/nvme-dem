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

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "common.h"

int get_logpages(struct ctrl_queue *dq, struct nvmf_disc_rsp_page_hdr **logp,
		 u32 *numrec)
{
	struct nvmf_disc_rsp_page_hdr	*log;
	unsigned int			 log_size = 0;
	unsigned long			 genctr;
	int				 ret;
	size_t				 offset;

	offset = offsetof(struct nvmf_disc_rsp_page_hdr, numrec);
	log_size = offset + sizeof(log->numrec);
	log_size = round_up(log_size, sizeof(u32));

	ret = send_get_log_page(&dq->ep, log_size, &log);
	if (ret) {
		print_err("failed to fetch number of discovery log entries");
		return -ENODATA;
	}

	genctr = le64toh(log->genctr);
	*numrec = le32toh(log->numrec);

	free(log);

	if (*numrec == 0) {
#ifdef DEBUG_LOG_PAGES_VERBOSE
		print_err("no discovery log on target %s", dq->target->alias);
#endif
		*logp = NULL;
		return 0;
	}

#ifdef DEBUG_LOG_PAGES_VERBOSE
	print_debug("number of records to fetch is %d", *numrec);
#endif

	log_size = sizeof(struct nvmf_disc_rsp_page_hdr) +
		   sizeof(struct nvmf_disc_rsp_page_entry) * *numrec;

	ret = send_get_log_page(&dq->ep, log_size, &log);
	if (ret) {
		print_err("failed to fetch discovery log entries");
		return -ENODATA;
	}

	if ((*numrec != le32toh(log->numrec)) ||
	    (genctr != le64toh(log->genctr))) {
		print_err("# records for last two get log pages not equal");
		return -EINVAL;
	}

	*logp = log;

	return 0;
}

void print_discovery_log(struct nvmf_disc_rsp_page_hdr *log, int numrec)
{
#ifdef DEBUG_LOG_PAGES
	int				 i;
	struct nvmf_disc_rsp_page_entry *e;

#ifdef DEBUG_LOG_PAGES_VERBOSE
	print_debug("%s %d, %s %" PRIu64,
		    "Discovery Log Number of Records", numrec,
		    "Generation counter", (uint64_t) le64toh(log->genctr));
#endif

	for (i = 0; i < numrec; i++) {
		e = &log->entries[i];

#ifdef DEBUG_LOG_PAGES_VERBOSE
		print_info("=====Discovery Log Entry %d======", i);
#endif
		print_info("trtype:  %s", trtype_str(e->trtype));
		print_info("adrfam:  %s", adrfam_str(e->adrfam));
		print_info("subtype: %s", subtype_str(e->subtype));
		print_info("treq:    %s", treq_str(e->treq));
		print_info("portid:  %d", e->portid);
		print_info("trsvcid: %s", e->trsvcid);
		print_info("subnqn:  %s", e->subnqn);
		print_info("traddr:  %s", e->traddr);

		switch (e->trtype) {
		case NVMF_TRTYPE_RDMA:
			print_info("rdma_prtype: %s",
				   prtype_str(e->tsas.rdma.prtype));
			print_info("rdma_qptype: %s",
				   qptype_str(e->tsas.rdma.qptype));
			print_info("rdma_cms:    %s",
				   cms_str(e->tsas.rdma.cms));
			print_info("rdma_pkey: 0x%04x",
				   e->tsas.rdma.pkey);
			break;
		}
	}
#else
	UNUSED(log);
	UNUSED(numrec);
#endif
}

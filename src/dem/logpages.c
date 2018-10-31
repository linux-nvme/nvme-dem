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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "common.h"

void del_unattached_logpage_list(struct target *target)
{
	struct logpage		*lp, *n;

	list_for_each_entry_safe(lp, n, &target->unattached_logpage_list,
				 node) {
		list_del(&lp->node);
		free(lp);
	}
}

static inline void invalidate_log_pages(struct target *target)
{
	struct subsystem		*subsys;
	struct logpage			*logpage;

	list_for_each_entry(subsys, &target->subsys_list, node)
		list_for_each_entry(logpage, &subsys->logpage_list, node)
			logpage->valid = 0;

	del_unattached_logpage_list(target);
}

static inline int match_logpage(struct logpage *logpage,
				struct nvmf_disc_rsp_page_entry *e)
{
	if (strcmp(e->traddr, logpage->e.traddr) ||
	    strcmp(e->trsvcid, logpage->e.trsvcid) ||
	    e->trtype != logpage->e.trtype ||
	    e->adrfam != logpage->e.adrfam)
		return 0;
	return 1;
}

static inline void store_logpage(struct logpage *logpage,
				 struct nvmf_disc_rsp_page_entry *e,
				 struct ctrl_queue *dq)
{
	logpage->e = *e;
	logpage->valid = 1;
	logpage->portid = dq->portid;
}

static void save_log_pages(struct nvmf_disc_rsp_page_hdr *log, int numrec,
			   struct target *target, struct ctrl_queue *dq)
{
	int				 i;
	int				 found;
	int				 create;
	struct subsystem		*subsys;
	struct logpage			*logpage, *lp;
	struct nvmf_disc_rsp_page_entry *e;

	for (i = 0; i < numrec; i++) {
		e = &log->entries[i];
		found = 0;
		create = 1;
		list_for_each_entry(subsys, &target->subsys_list, node)
			if ((strcmp(subsys->nqn, e->subnqn) == 0)) {
				found = 1;
				list_for_each_entry(logpage,
						    &subsys->logpage_list,
						    node)
					if (match_logpage(logpage, e)) {
						store_logpage(logpage, e, dq);
						create = 0;
						goto next;
					}
next:
				break;
			}

		if (!create)
			continue;

		logpage = malloc(sizeof(*logpage));
		if (!logpage) {
			print_err("alloc new logpage failed");
			return;
		}

		store_logpage(logpage, e, dq);

		if (found) {
			list_add_tail(&logpage->node, &subsys->logpage_list);
			continue;
		}

		list_for_each_entry(lp, &target->unattached_logpage_list, node)
			if (!strcmp(lp->e.subnqn, e->subnqn) &&
			    match_logpage(lp, e)) {
				found = 1;
				free(logpage);
				break;
			}

		if (!found)
			list_add_tail(&logpage->node,
				      &target->unattached_logpage_list);
	}
}

void fetch_log_pages(struct ctrl_queue *dq)
{
	struct nvmf_disc_rsp_page_hdr	*log = NULL;
	struct target			*target = dq->target;
	u32				 num_records = 0;

	if (get_logpages(dq, &log, &num_records)) {
		print_err("get logpages for target %s failed", target->alias);
		return;
	}

	save_log_pages(log, num_records, target, dq);

	print_discovery_log(log, num_records);

	free(log);
}

static int target_with_allow_any_subsys(struct target *target)
{
	struct subsystem		*subsys;

	list_for_each_entry(subsys, &target->subsys_list, node)
		if (!is_restricted(subsys))
			return 1;

	return 0;
}

static int avilable_dq(struct ctrl_queue *dq)
{
	if (!dq->subsys)
		return target_with_allow_any_subsys(dq->target);

	return !list_empty(&dq->subsys->host_list);
}

void refresh_log_pages(struct target *target)
{
	struct ctrl_queue	*dq;

	invalidate_log_pages(target);

	list_for_each_entry(dq, &target->discovery_queue_list, node) {
		if (!dq->connected) {
			if (!avilable_dq(dq))
				continue;
			if (connect_ctrl(dq)) {
				target->log_page_retry_count = LOG_PAGE_RETRY;
				continue;
			}
		}

		fetch_log_pages(dq);

		if (dq->failed_kato)
			disconnect_ctrl(dq, 0);
	}
}

static void format_logpage(char *buf, struct nvmf_disc_rsp_page_entry *e)
{
	int			 n;
	char			*p = buf;

	n = sprintf(p, "<p>subnqn=<b>\"%s\"</b> ", e->subnqn);
	p += n;
	n = sprintf(p, "subtype=<b>\"%s\"</b> ", subtype_str(e->subtype));
	p += n;
	n = sprintf(p, "portid=<b>%d</b> ", e->portid);
	p += n;
	n = sprintf(p, "trtype=<b>\"%s\"</b> ", trtype_str(e->trtype));
	p += n;
	n = sprintf(p, "adrfam=<b>\"%s\"</b> ", adrfam_str(e->adrfam));
	p += n;
	n = sprintf(p, "traddr=<b>%s</b> ", e->traddr);
	p += n;
	n = sprintf(p, "trsvcid=<b>%s</b> ", e->trsvcid);
	p += n;
	n = sprintf(p, "treq=<b>\"%s\"</b><br>", treq_str(e->treq));
	p += n;

	switch (e->trtype) {
	case NVMF_TRTYPE_RDMA:
		n = sprintf(p, " &nbsp; rdma: ");
		p += n;
		n = sprintf(p, "prtype=<b>\"%s\"</b> ",
			    prtype_str(e->tsas.rdma.prtype));
		p += n;
		n = sprintf(p, "qptype=<b>\"%s\"</b> ",
			    qptype_str(e->tsas.rdma.qptype));
		p += n;
		n = sprintf(p, "cms=<b>\"%s\"</b> ", cms_str(e->tsas.rdma.cms));
		p += n;
		n = sprintf(p, "pkey=<b>0x%04x</b>", e->tsas.rdma.pkey);
		p += n;
		break;
	}
	n = sprintf(p, "</p>");
	p += n;
}

int target_logpage(char *alias, char **resp)
{
	struct target		*target;
	struct subsystem	*subsys;
	struct logpage		*logpage;
	char			 buf[MAX_BODY_SIZE + 1];
	char			*p = *resp;
	u64			 n, len = 0;
	u64			 bytes = MAX_BODY_SIZE;

	list_for_each_entry(target, target_list, node)
		if (!strcmp(target->alias, alias))
			goto found;

	return -ENOENT;
found:
	list_for_each_entry(subsys, &target->subsys_list, node)
		list_for_each_entry(logpage, &subsys->logpage_list, node) {
			if (!logpage->valid)
				continue;

			format_logpage(buf, &logpage->e);

			if ((len + strlen(buf)) > bytes) {
				bytes += MAX_BODY_SIZE;
				p = realloc(*resp, bytes);
				if (!p)
					return -ENOMEM;
				*resp = p;
				p += len;
			}
			strcpy(p, buf);
			n = strlen(buf);
			p += n;
			len += n;
		}

	if (list_empty(&target->unattached_logpage_list))
		goto out;

	sprintf(buf, "<p><p><b style='color:red'>Unattached Log Pages</b><p>");
	if ((len + strlen(buf)) > bytes) {
		bytes += MAX_BODY_SIZE;
		p = realloc(*resp, bytes);
		if (!p)
			return -ENOMEM;
		*resp = p;
		p += len;
	}
	strcpy(p, buf);
	n = strlen(buf);
	p += n;
	len += n;

	list_for_each_entry(logpage, &target->unattached_logpage_list, node) {
		format_logpage(buf, &logpage->e);

		if ((len + strlen(buf)) > bytes) {
			bytes += MAX_BODY_SIZE;
			p = realloc(*resp, bytes);
			if (!p)
				return -ENOMEM;
			*resp = p;
			p += len;
		}
		strcpy(p, buf);
		n = strlen(buf);
		p += n;
		len += n;
	}

out:
	if (!len)
		sprintf(*resp, "No valid Log Pages");

	return 0;
}

int host_logpage(char *alias, char **resp)
{
	struct host		*host;
	struct target		*target;
	struct subsystem	*subsys;
	struct logpage		*logpage;
	char			 buf[MAX_BODY_SIZE + 1];
	char			*p = *resp;
	u64			 n, len = 0;
	u64			 bytes = MAX_BODY_SIZE;

	list_for_each_entry(target, target_list, node) {
		if (target->group_member &&
		    !indirect_shared_group(target, alias))
			continue;

		list_for_each_entry(subsys, &target->subsys_list, node) {
			if (!is_restricted(subsys))
				goto found;

			list_for_each_entry(host, &subsys->host_list, node)
				if (!strcmp(alias, host->alias))
					goto found;
			continue;
found:
			list_for_each_entry(logpage, &subsys->logpage_list,
					    node) {
				if (!logpage->valid)
					continue;

				format_logpage(buf, &logpage->e);

				if ((len + strlen(buf)) > bytes) {
					bytes += MAX_BODY_SIZE;
					p = realloc(*resp, bytes);
					if (!p)
						return -ENOMEM;
					*resp = p;
					p += len;
				}
				strcpy(p, buf);
				n = strlen(buf);
				p += n;
				len += n;
			}
		}
	}

	if (!len)
		sprintf(*resp, "No valid Log Pages");

	return 0;
}

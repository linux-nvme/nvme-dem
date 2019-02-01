// SPDX-License-Identifier: DUAL GPL-2.0/BSD
/*
 * NVMe over Fabrics Distributed Endpoint Management (NVMe-oF DEM).
 * Copyright (c) 2017-2019 Intel Corporation, Inc. All rights reserved.
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

#include "common.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "tcp.h"
#include "ops.h"

#define BACKLOG			16
#define RESOLVE_TIMEOUT		5000
#define EVENT_TIMEOUT		200

#define TCP_SYNCNT		7
#define TCP_NODELAY		1

struct tcp_qe {
	void			*buf;
	union nvme_tcp_pdu	 pdu;
};

struct tcp_ep {
	struct sockaddr_in	*sock_addr;
	struct tcp_qe		*qe;
	int			 sockfd;
	int			 state;
	__u64			 depth;
};

struct tcp_pep {
	struct sockaddr_in	*sock_addr;
	int			 listenfd;
	int			 sockfd;
};

static int tcp_create_queue_recv_pool(struct tcp_ep *ep)
{
	struct tcp_qe		*qe;
	u16			 i;

	qe = calloc(sizeof(struct tcp_qe), ep->depth);
	if (!qe)
		goto err1;

	for (i = 0; i < ep->depth; i++) {
		qe[i].buf = malloc(PAGE_SIZE);
		if (!qe[i].buf)
			goto err2;
	}

	ep->qe = qe;

	return 0;
err2:
	while (i > 0)
		free(qe[--i].buf);

	free(qe);
err1:
	return -ENOMEM;
}

static int tcp_init_endpoint(struct xp_ep **_ep, int depth)
{
	struct tcp_ep		*ep;
	int			 sockfd;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		print_err("Error: Cannot create the socket");
		return -errno;
	}

	ep = malloc(sizeof(*ep));
	if (!ep) {
		close(sockfd);
		return -ENOMEM;
	}

	memset(ep, 0, sizeof(*ep));

	*_ep = (struct xp_ep *) ep;

	ep->sockfd = sockfd;
	ep->depth = depth;

	return 0;
}

static void tcp_destroy_endpoint(struct xp_ep *_ep)
{
	struct tcp_ep		*ep = (struct tcp_ep *) _ep;
	struct tcp_qe		*qe = ep->qe;
	int			 i = ep->depth;

	if (qe) {
		while (i > 0)
			if (qe[--i].buf)
				free(qe[i].buf);
		free(qe);
	}

	close(ep->sockfd);

	free(ep);
}

static int tcp_create_endpoint(struct xp_ep **_ep, void *id, int depth)
{
	struct tcp_ep		*ep;
	int			 flags;

	UNUSED(depth);

	ep = malloc(sizeof(*ep));
	if (!ep)
		return -ENOMEM;

	memset(ep, 0, sizeof(*ep));

	ep->sockfd = *(int *) id;

	flags = fcntl(ep->sockfd, F_GETFL);
	fcntl(ep->sockfd, F_SETFL, flags | O_NONBLOCK);

	*_ep = (struct xp_ep *) ep;

	return 0;
}

static int tcp_init_listener(struct xp_pep **_pep, char *srvc)
{
	struct tcp_pep		*pep;
	struct sockaddr_in	 addr;
	int			 listenfd;
	int			 ret;

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(srvc));
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	listenfd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
	if (listenfd < 0) {
		print_err("Socket error %d", errno);
		return -errno;
	}

	ret = bind(listenfd, (struct sockaddr *) &addr, sizeof(addr));
	if (ret < 0) {
		print_err("Socket bind error %d", errno);
		goto err;
	}

	ret = listen(listenfd, BACKLOG);
	if (ret) {
		print_err("Socket listen error %d", errno);
		goto err;
	}

	pep = malloc(sizeof(*pep));
	if (!pep) {
		ret = -ENOMEM;
		goto err;
	}

	memset(pep, 0, sizeof(*pep));

	*_pep = (struct xp_pep *) pep;

	pep->listenfd = listenfd;

	return 0;
err:
	close(listenfd);
	return ret;
}

static int tcp_accept_connection(struct xp_ep *_ep)
{
	struct tcp_ep		*ep = (struct tcp_ep *) _ep;
	struct nvme_tcp_icreq_pdu *init_req = NULL;
	struct nvme_tcp_icresp_pdu *init_rep;
	struct nvme_tcp_common_hdr *hdr;
	unsigned int		 digest = 0;
	int			 ret, len;

	if (!ep)
		return -EINVAL;

	hdr = malloc(sizeof(*hdr));
	if (!hdr)
		return -ENOMEM;

	memset(hdr, 0, sizeof(*hdr));

	len = sizeof(*hdr);
	ret = read(ep->sockfd, hdr, len);
	if (ret != sizeof(*hdr)) {
		ret = (ret < 0) ? -EAGAIN : -ENODATA;
		goto err1;
	}

	if (hdr->pdu_type == 0) {
		if (posix_memalign((void **) &init_req, PAGE_SIZE,
					sizeof(*init_req))) {
			ret = -ENOMEM;
			goto err1;
		}
		len = sizeof(*init_req)-sizeof(*hdr);
		ret = read(ep->sockfd, init_req, len);
		if (ret != len) {
			ret = -ENODATA;
			goto err2;
		}
		if (init_req->hpda != 0) {
			ret = -EPROTO;
			goto err2;
		}
	}

	if (posix_memalign((void **) &init_rep, PAGE_SIZE,
				sizeof(*init_rep))) {
		ret = -ENOMEM;
		goto err2;
	}

	init_rep->c_hdr.pdu_type = NVME_TCP_ICRESP;
	init_rep->c_hdr.hlen = sizeof(*init_rep);
	init_rep->c_hdr.pdo = 0;
	init_rep->c_hdr.plen = htole32(sizeof(*init_rep));
	init_rep->pfv = htole16(NVME_TCP_PDU_FORMAT_VER);
	init_rep->maxh2c = 0xffff;
	init_rep->cpda = 0;
	digest = 0;
	init_rep->dgst = htole16(digest);

	ret = write(ep->sockfd, init_rep, sizeof(*init_rep));
	if (ret < 0)
		goto err3;

	return 0;
err3:
	free(init_rep);
err2:
	if (init_req)
		free(init_req);
err1:
	free(hdr);
	return ret;
}

static int tcp_reject_connection(struct xp_ep *_ep, void *data, int len)
{
	// TBD Verify if there is a reject command for tcp
	UNUSED(_ep);
	UNUSED(data);
	UNUSED(len);

	return 0;
}

static int tcp_wait_for_connection(struct xp_pep *_pep, void **_id)
{
	struct tcp_pep		*pep = (struct tcp_pep *) _pep;
	int			 sockfd;
	int			*id;
	int			 ret;

	id = malloc(sizeof(int));
	if (!id)
		return -ENOMEM;

	while (true) {
		usleep(100); //TBD
		if (stopped) {
			ret = -ESHUTDOWN;
			goto err;
		}

		sockfd = accept(pep->listenfd, (struct sockaddr *) NULL,
				NULL);
		if (sockfd < 0) {
			if (errno != EAGAIN)
				print_err("failed to accept err=%d\n",
						sockfd);
			ret = -EAGAIN;
			goto err;
		}

		pep->sockfd = sockfd;
		*id = sockfd;
		*_id = id;
		return 0;
	}
err:
	free(id);
	return ret;
}

static int validate_reply(struct nvme_tcp_icresp_pdu *reply, int len)
{

	if (reply->c_hdr.pdu_type != NVME_TCP_ICRESP) {
		print_err("client connect bad type %d", reply->c_hdr.pdu_type);
		return -EINVAL;
	}

	if (len != sizeof(struct nvme_tcp_icresp_pdu)) {
		print_err("bad len %d", len);
		return -EINVAL;
	}

	if (reply->c_hdr.plen != sizeof(struct nvme_tcp_icresp_pdu)) {
		print_err("bad plen %d", reply->c_hdr.plen);
		return -EINVAL;
	}

	if (reply->c_hdr.hlen != sizeof(struct nvme_tcp_icresp_pdu)) {
		print_err("bad hlen %d", reply->c_hdr.hlen);
		return -EINVAL;
	}

	if (reply->pfv != NVME_TCP_CONNECT_FMT_1_0) {
		print_err("bad pfv %d", reply->pfv);
		return -EINVAL;
	}

	if (reply->dgst != 0) {
		print_err("unsupported digest %d", reply->dgst);
		return -EINVAL;
	}

	if (reply->cpda != 0) {
		print_err("unsupported cpda %d", reply->cpda);
		return -EINVAL;
	}

	return 0;
}

static int tcp_client_connect(struct xp_ep *_ep, struct sockaddr *dst,
			       void *data, int _len)
{
	struct tcp_ep		*ep = (struct tcp_ep *) _ep;
	struct nvme_tcp_icresp_pdu *reply;
	struct nvme_tcp_icreq_pdu *conn;
	int			 len, ret;
	int			 opt = 1;

	ret = setsockopt(ep->sockfd, IPPROTO_TCP, TCP_SYNCNT,
			 (char *) &opt, sizeof(opt));
	if (ret != 0) {
		print_err("setsockopt TCP_SYNCNT returned %d", errno);
		return -errno;
	}

	ret = setsockopt(ep->sockfd, IPPROTO_TCP,
			TCP_NODELAY, (char *) &opt, sizeof(opt));
	if (ret != 0) {
		print_err("setsockopt TCP_NODELAY returned %d", errno);
		return -errno;
	}

	ret = connect(ep->sockfd, (struct sockaddr *) dst, sizeof(*dst));
	if (ret != 0)
		return -errno;

	conn = (struct nvme_tcp_icreq_pdu *) data;

	len = write(ep->sockfd, conn, _len);
	if (len < 0) {
		print_err("write returned %d", errno);
		return -errno;
	}

	if (posix_memalign((void **) &reply, PAGE_SIZE, sizeof(*reply)))
		return -ENOMEM;

	len = read(ep->sockfd, reply, sizeof(*reply));
	if (len < 0) {
		print_err("read returned %d", errno);
		ret = -errno;
		goto out;
	}

	ret = validate_reply(reply, len);
	if (ret != -EINVAL) {
		ep->state = CONNECTED;
		ret = 0;
	}
out:
	free(reply);
	return ret;
}

static void tcp_destroy_listener(struct xp_pep *_pep)
{
	struct tcp_pep		*pep = (struct tcp_pep *) _pep;

	close(pep->listenfd);
	free(pep->sock_addr);
}

static int tcp_rma_read(struct xp_ep *_ep, void *buf, u64 addr, u64 _len,
			 u32 rkey, struct xp_mr *_mr)
{
	struct tcp_ep		*ep = (struct tcp_ep *) _ep;
	int			 len;

	UNUSED(addr);
	UNUSED(rkey);
	UNUSED(_mr);

	len = read(ep->sockfd, buf, _len);
	if (len < 0) {
		print_err("read returned %d", errno);
		return -errno;
	}

	return 0;
}

static int tcp_rma_write(struct xp_ep *_ep, void *buf, u64 addr, u64 _len,
		u32 rkey, struct xp_mr *_mr, struct nvme_command *cmd)
{
	struct tcp_ep		*ep = (struct tcp_ep *) _ep;
	struct nvme_tcp_data_pdu pdu;
	int			 len;

	UNUSED(_mr);
	UNUSED(addr);
	UNUSED(rkey);

	pdu.c_hdr.pdu_type = NVME_TCP_C2HDATA;
	pdu.c_hdr.flags = 0;
	pdu.c_hdr.pdo = 0;
	pdu.c_hdr.hlen = sizeof(struct nvme_tcp_data_pdu);
	pdu.c_hdr.plen = sizeof(struct nvme_tcp_data_pdu) + _len;
	pdu.data_offset = 0;
	pdu.data_length = _len;
	pdu.cccid = cmd->common.command_id;

	len = write(ep->sockfd, &pdu, sizeof(pdu));
	if (len < 0) {
		print_err("header write returned %d", errno);
		return -errno;
	}

	len = write(ep->sockfd, buf, _len);
	if (len < 0) {
		print_err("data write returned %d", errno);
		return -errno;
	}

	return 0;
}

static int tcp_repost_recv(struct xp_ep *_ep, struct xp_qe *_qe)
{
	UNUSED(_ep);
	UNUSED(_qe);

	return 0;
}

static int tcp_inline_write(size_t sockfd, void *data, size_t _len)
{
	int			 len;

	len = write(sockfd, (const void *) data, _len);
	if (len < 0) {
		print_err("data write returned %d", errno);
		return -errno;
	}

	return 0;
}

static int tcp_inline_read(size_t sockfd, void *data, size_t _len)
{
	struct nvme_tcp_data_pdu d_pdu;
	int			 len;

	UNUSED(_len);

	len = read(sockfd, &d_pdu, sizeof(d_pdu));
	if (len != sizeof(d_pdu)) {
		print_err("header read returned %d", errno);
		return (len < 0) ? -errno : -ENODATA;
	}

	while (d_pdu.data_length > 0) {
		len = read(sockfd, (char *) data + d_pdu.data_offset,
			   d_pdu.data_length);
		if (len < 0) {
			if (errno == EAGAIN)
				continue;

			print_err("data read returned %d", errno);
			return -errno;
		}

		d_pdu.data_length -= len;
		d_pdu.data_offset += len;
	}

	return 0;
}

static inline int tcp_handle_inline_data(struct tcp_ep *ep,
					struct nvme_command *cmd)
{
	struct nvme_sgl_desc	*sg = &cmd->common.dptr.sgl;
	char			*data = (char *)sg->addr;
	int			 length = sg->length;
	int			 direction;
	int			 ret;

	if (cmd->common.opcode == nvme_fabrics_command)
		direction = cmd->fabrics.fctype & NVME_OPCODE_MASK;
	else
		direction = cmd->common.opcode & NVME_OPCODE_MASK;

	if (direction == NVME_OPCODE_C2H)
		ret = tcp_inline_write(ep->sockfd, data, length);
	else if (direction == NVME_OPCODE_H2C)
		ret = tcp_inline_read(ep->sockfd, data, length);
	else
		ret = 0;

	return ret;
}

static int tcp_send_msg(struct xp_ep *_ep, void *msg, int _len,
			struct xp_mr *_mr)
{
	struct nvme_command	*cmd = (struct nvme_command *)msg;
	struct tcp_ep		*ep = (struct tcp_ep *)_ep;
	struct nvme_tcp_cmd_capsule_pdu	 pdu;
	int			 len;

	UNUSED(_len);
	UNUSED(_mr);

	pdu.c_hdr.pdu_type = NVME_TCP_CAPSULECMD;
	pdu.c_hdr.flags = 0;
	pdu.c_hdr.pdo = 0;
	pdu.c_hdr.hlen = sizeof(struct nvme_tcp_cmd_capsule_pdu);
	pdu.c_hdr.plen = sizeof(struct nvme_command) +
			 sizeof(struct nvme_tcp_common_hdr);

	memcpy(&(pdu.cmd), cmd, sizeof(struct nvme_command));

	len = write(ep->sockfd, &pdu, sizeof(pdu));
	if (len != sizeof(pdu)) {
		print_err("write command returned %d", errno);
		return -errno;
	}

	return tcp_handle_inline_data(ep, cmd);
}

static int tcp_send_rsp(struct xp_ep *_ep, void *msg, int _len,
			struct xp_mr *_mr)
{
	struct nvme_completion  *comp = (struct nvme_completion *)msg;
	struct tcp_ep		*ep = (struct tcp_ep *) _ep;
	struct nvme_tcp_resp_capsule_pdu pdu;
	int			 len;

	UNUSED(_mr);
	UNUSED(msg);
	UNUSED(_len);

	pdu.c_hdr.pdu_type = NVME_TCP_CAPSULERESP;
	pdu.c_hdr.flags = 0;
	pdu.c_hdr.pdo = 0;
	pdu.c_hdr.hlen = sizeof(struct nvme_tcp_resp_capsule_pdu);
	pdu.c_hdr.plen = sizeof(struct nvme_tcp_resp_capsule_pdu);

	memcpy(&(pdu.cqe), comp, sizeof(struct nvme_completion));

	len = write(ep->sockfd, &pdu, sizeof(pdu));
	if (len != sizeof(pdu)) {
		print_err("write completion returned %d", errno);
		return -errno;
	}

	return 0;
}

static int tcp_poll_for_msg(struct xp_ep *_ep, struct xp_qe **_qe,
			    void **_msg, int *bytes)
{
	struct tcp_ep		*ep = (struct tcp_ep *) _ep;
	struct nvme_tcp_common_hdr hdr;
	void			*msg;
	int			 msg_len;
	int			 len;

	UNUSED(_qe);

	len = read(ep->sockfd, &hdr, sizeof(hdr));
	if (len != sizeof(hdr)) {
		return (len < 0) ? -errno : -ENODATA;
	}

	msg_len = hdr.hlen - sizeof(hdr);
	if (msg_len <= 0)
		return msg_len;

	if (posix_memalign(&msg, PAGE_SIZE, msg_len))
		return -ENOMEM;

	len = read(ep->sockfd, msg, msg_len);
	if (len == 0)
		return -EAGAIN;
	if (len < 0)
		return -errno;
	*_msg = msg;
	*bytes = msg_len;

	return 0;
}

static int tcp_alloc_key(struct xp_ep *_ep, void *buf, int len,
			 struct xp_mr **_mr)
{
	UNUSED(_ep);
	UNUSED(buf);
	UNUSED(len);
	UNUSED(_mr);

	return 0;
}

static u32 tcp_remote_key(struct xp_mr *_mr)
{
	UNUSED(_mr);

	return 0;
}

static int tcp_dealloc_key(struct xp_mr *_mr)
{
	UNUSED(_mr);

	return 0;
}

static int tcp_build_connect_data(void **req, char *hostnqn)
{
	struct nvme_tcp_icreq_pdu *connect;
	int			 bytes = sizeof(*connect);

	UNUSED(hostnqn); // TBD check with hostnqn

	if (posix_memalign((void **) &connect, PAGE_SIZE, bytes))
		return -errno;

	connect->c_hdr.pdu_type = NVME_TCP_ICREQ;
	connect->c_hdr.hlen = sizeof(*connect);
	connect->c_hdr.pdo = 0;
	connect->c_hdr.plen = htole32(sizeof(*connect));

	connect->pfv = htole16(NVME_TCP_CONNECT_FMT_1_0);
	connect->maxr2t = 0;
	connect->dgst = 0;
	connect->hpda = 0;

	*req = connect;

	return bytes;
}

static void tcp_set_sgl(struct nvme_command *cmd, u8 opcode, int len,
			void *data, int key)
{
	struct nvme_sgl_desc	*sg;

	UNUSED(key);

	memset(cmd, 0, sizeof(*cmd));

	cmd->common.flags = NVME_CMD_SGL_METABUF;
	cmd->common.opcode = opcode;

	sg = &cmd->common.dptr.sgl;
	sg->length = htole32(len);

	if (opcode == nvme_admin_get_log_page)
		sg->type = (NVME_TRANSPORT_SGL_DATA_DESC << 4) |
			   NVME_SGL_FMT_TRANSPORT_A;
	else
		sg->type = (NVME_SGL_FMT_DATA_DESC << 4) |
			   NVME_SGL_FMT_OFFSET;

	sg->addr = (u64) data;
}

static struct xp_ops tcp_ops = {
	.init_endpoint		= tcp_init_endpoint,
	.create_endpoint	= tcp_create_endpoint,
	.destroy_endpoint	= tcp_destroy_endpoint,
	.init_listener		= tcp_init_listener,
	.destroy_listener	= tcp_destroy_listener,
	.wait_for_connection	= tcp_wait_for_connection,
	.accept_connection	= tcp_accept_connection,
	.reject_connection	= tcp_reject_connection,
	.client_connect		= tcp_client_connect,
	.rma_read		= tcp_rma_read,
	.rma_write		= tcp_rma_write,
	.repost_recv		= tcp_repost_recv,
	.post_msg		= tcp_send_msg,
	.send_msg		= tcp_send_msg,
	.send_rsp		= tcp_send_rsp,
	.poll_for_msg		= tcp_poll_for_msg,
	.alloc_key		= tcp_alloc_key,
	.remote_key		= tcp_remote_key,
	.dealloc_key		= tcp_dealloc_key,
	.build_connect_data	= tcp_build_connect_data,
	.set_sgl		= tcp_set_sgl,
};

struct xp_ops *tcp_register_ops(void)
{
	return &tcp_ops;
}

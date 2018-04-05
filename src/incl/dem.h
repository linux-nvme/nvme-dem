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

#ifndef __DEM_H__
#define __DEM_H__

#define unlikely __glibc_unlikely

#define NVMF_UUID_FMT		"nqn.2014-08.org.nvmexpress:NVMf:uuid:%s"

#define PAGE_SIZE		4096
#define BUF_SIZE		4096
#define BODY_SIZE		1024
#define NVMF_DQ_DEPTH		1
#define IDLE_TIMEOUT		100
#define MINUTES			(60 * 1000) /* convert ms to minutes */
#define LOG_PAGE_RETRY		200

#define NULLB_DEVID		-1

// TODO disable DEV_DEBUG before pushing to gitlab
#if 1
#define DEV_DEBUG
#endif

#define CONFIG_DIR		"/etc/nvme/nvmeof-dem/"
#define CONFIG_FILENAME		"config"
#define SIGNATURE_FILE_FILENAME	"signature"
#define CONFIG_FILE		(CONFIG_DIR CONFIG_FILENAME)
#define SIGNATURE_FILE		(CONFIG_DIR SIGNATURE_FILE_FILENAME)

#define print_debug(f, x...) \
	do { \
		if (debug) { \
			printf("%s(%d) " f "\n", __func__, __LINE__, ##x); \
			fflush(stdout); \
		} \
	} while (0)
#define print_trace()\
	do { \
		printf("%s(%d)\n", __func__, __LINE__); \
		fflush(stdout); \
	} while (0)
#define print_info(f, x...)\
	do { \
		printf(f "\n", ##x); \
		fflush(stdout); \
	} while (0)
#define print_err(f, x...)\
	do { \
		fprintf(stderr, "Error: " f "\n", ##x); \
		fflush(stderr); \
	} while (0)

#define UNUSED(x) ((void) x)

#define min(x, y) ((x < y) ? x : y)

#define __round_mask(x, y) ((__typeof__(x))((y) - 1))
#define round_up(x, y) ((((x) - 1) | __round_mask(x, y)) + 1)

extern int			 stopped;

enum { DISCONNECTED, CONNECTED };

static inline u32 get_unaligned_le24(const u8 *p)
{
	return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16;
}

static inline u32 get_unaligned_le32(const u8 *p)
{
	return (u32) p[0] | (u32) p[1] << 8 |
		(u32) p[2] << 16 | (u32) p[3] << 24;
}

static inline int msec_delta(struct timeval t0)
{
	struct timeval		t1;

	gettimeofday(&t1, NULL);

	return (t1.tv_sec - t0.tv_sec) * 1000 +
		(t1.tv_usec - t0.tv_usec) / 1000;
}

/*HACK*/
#define PATH_NVME_FABRICS	"/dev/nvme-fabrics"
#define PATH_NVMF_DEM_DISC	"/etc/nvme/nvmeof-dem/"
#define NUM_CONFIG_ITEMS	3
#define CONFIG_TYPE_SIZE	8
#define CONFIG_FAMILY_SIZE	8
#define CONFIG_ADDRESS_SIZE	40
#define CONFIG_PORT_SIZE	8
#define CONFIG_DEVICE_SIZE	256
#define LARGEST_TAG		8
#define LARGEST_VAL		40
#define ADDR_LEN		16 /* IPV6 is current longest address */

#define MAX_BODY_SIZE		1024
#define MAX_URI_SIZE		128
#define MAX_NQN_SIZE		256
#define MAX_ALIAS_SIZE		64

#ifndef AF_IPV4
#define AF_IPV4			1
#define AF_IPV6			2
#endif

#ifndef AF_FC
#define AF_FC			3
#endif

/* HACK - Figure out which of these we need */
#define DISC_BUF_SIZE		4096
#define PATH_NVME_FABRICS	"/dev/nvme-fabrics"
#define PATH_NVMF_DISC		"/etc/nvme/discovery.conf"
#define PATH_NVMF_HOSTNQN	"/etc/nvme/hostnqn"
#define SYS_NVME		"/sys/class/nvme"

struct target;
struct host_iface;
struct portid;

struct qe {
	struct xp_qe		*qe;
	u8			*buf;
};

struct endpoint {
	struct xp_ep		*ep;
	struct xp_mr		*mr;
	struct xp_mr		*data_mr;
	struct xp_ops		*ops;
	struct nvme_command	*cmd;
	struct qe		*qe;
	void			*data;
	char			 nqn[MAX_NQN_SIZE + 1];
	int			 depth;
	int			 state;
	int			 csts;
};

struct ctrl_queue {
	struct list_head	 node;
	struct portid		*portid;
	struct target		*target;
	struct endpoint		 ep;
	char			 hostnqn[MAX_NQN_SIZE + 1];
	int			 connected;
	int			 failed_kato;
};

enum { LOCAL_MGMT = 0, IN_BAND_MGMT, OUT_OF_BAND_MGMT, DISCOVERY_CTRL };

int parse_line(FILE *fd, char *tag, int tag_max, char *value, int value_max);

int ipv4_to_addr(char *p, int *addr);
int ipv6_to_addr(char *p, int *addr);
int fc_to_addr(char *p, int *addr);

int connect_ctrl(struct ctrl_queue *ctrl);
void disconnect_ctrl(struct ctrl_queue *ctrl, int shutdown);
int client_connect(struct endpoint *ep, void *data, int bytes);
void disconnect_endpoint(struct endpoint *ep, int shutdown);

int send_get_log_page(struct endpoint *ep, int log_size,
		      struct nvmf_disc_rsp_page_hdr **log);
int send_keep_alive(struct endpoint *ep);
int send_reset_config(struct endpoint *ep);
int send_set_config(struct endpoint *ep, int cid, int len, void *data);
int send_get_config(struct endpoint *ep, int cid, int len, void **data);

int send_del_target(struct target *target);

void print_discovery_log(struct nvmf_disc_rsp_page_hdr *log, int numrec);
int get_logpages(struct ctrl_queue *dq, struct nvmf_disc_rsp_page_hdr **logp,
		 u32 *numrec);

const char *trtype_str(u8 trtype);
const char *adrfam_str(u8 adrfam);
const char *subtype_str(u8 subtype);
const char *treq_str(u8 treq);
const char *prtype_str(u8 prtype);
const char *qptype_str(u8 qptype);
const char *cms_str(u8 cm);
u8 to_trtype(char *str);
u8 to_adrfam(char *str);

void dump(u8 *buf, int len);

#endif

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

#include "common.h"
#include "tags.h"

#define is_whitespace(c) (c == ' ' || c == '\t' || c == '\r')
#define is_quote(c)	 (c == '\'' || c == '"')
#define is_eol(c)	 (c == '\n' || c == EOF)

#define COMMENT		'#'
#define EQUAL		'='
#define MAX_EQUAL	2

#define IPV4_LEN	4
#define IPV4_WIDTH	3
#define IPV4_CHAR	'.'

#define IPV6_LEN	8
#define IPV6_WIDTH	4
#define IPV6_CHAR	':'

#define FC_LEN		8
#define FC_WIDTH	2
#define FC_CHAR		':'

static int consume_line(FILE *fd)
{
	char			 c;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (is_eol(c))
			return 0;
	}
	return 1;
}

static int consume_whitespace(FILE *fd)
{
	char			 c;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT)
			return 1;

		if (!is_whitespace(c) || is_eol(c)) {
			ungetc(c, fd);
			return 0;
		}
	}
	return 1;
}

static int parse_word(FILE *fd, char *word, int n)
{
	char			 c, *p = word;
	int			 quoted = 0;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT && !quoted)
			break;

		if (is_quote(c)) {
			quoted = ~quoted;
			continue;
		}

		if (is_whitespace(c) || is_eol(c) || c == EQUAL) {
			ungetc(c, fd);
			break;
		}

		*p++ = c;

		if (--n == 0)
			break;
	}

	if (p == word)
		return 1;

	*p = 0;

	return 0;
}

static int parse_equal(FILE *fd, char *word, int n)
{
	char			 c, *p = word;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT)
			break;

		if (c != EQUAL) {
			ungetc(c, fd);
			break;
		}

		*p++ = c;

		if (--n == 0)
			break;
	}

	if (p == word)
		return 1;

	*p = 0;

	return 0;
}

int parse_line(FILE *fd, char *tag, int tag_max, char *value, int value_max)
{
	char			 equal[MAX_EQUAL + 1];
	int			 ret;

	ret = consume_whitespace(fd);
	if (ret)
		goto out;

	ret = parse_word(fd, tag, tag_max);
	if (ret)
		goto out;

	ret = consume_whitespace(fd);
	if (ret)
		goto out;

	ret = parse_equal(fd, equal, MAX_EQUAL);
	if (ret)
		goto out;

	if (strcmp(equal, "=") != 0) {
		ret = -1;
		goto out;
	}

	ret = consume_whitespace(fd);
	if (ret)
		goto out;

	ret = parse_word(fd, value, value_max);
out:
	consume_line(fd);

	return ret;
}

static int string_to_addr(char *p, int *addr, int len, int width, char delim)
{
	char			 nibble[8];
	int			 i, j, n, z;

	n = strlen(p) + 1;

	for (z = j = i = 0; i < n && j < len; i++, p++) {
		if (*p == delim || *p == 0 || *p == '/') {
			if (z) {
				if (z > width)
					return -1;
				nibble[z] = 0;
				z = 0;
				addr[j] = atoi(nibble);
			} else
				addr[j] = 0;

			j++;

			if (*p == '/')
				break;
		} else
			nibble[z++] = *p;
	}

	for (; j < len; j++)
		addr[j] = 0;

	if (i < n && *p == '/') {
		*p++ = 0;
		return atoi(p);
	}

	return 0;
}

int ipv4_to_addr(char *p, int *addr)
{
	return string_to_addr(p, addr, IPV4_LEN, IPV4_WIDTH, IPV4_CHAR);
}

int ipv6_to_addr(char *p, int *addr)
{
	return string_to_addr(p, addr, IPV6_LEN, IPV6_WIDTH, IPV6_CHAR);
}

int fc_to_addr(char *p, int *addr)
{
	return string_to_addr(p, addr, FC_LEN, FC_WIDTH, FC_CHAR);
}

static const char *arg_str(const char * const *strings, size_t array_size,
			   size_t idx)
{
	if (idx < array_size && strings[idx])
		return strings[idx];

	return "unrecognized";
}

static const char * const trtypes[] = {
	[NVMF_TRTYPE_RDMA]		 = "rdma",
	[NVMF_TRTYPE_FC]		 = "fc",
	[NVMF_TRTYPE_LOOP]		 = "loop",
};

const char *trtype_str(u8 trtype)
{
	return arg_str(trtypes, ARRAY_SIZE(trtypes), trtype);
}

static const char * const adrfams[] = {
	[NVMF_ADDR_FAMILY_PCI]		 = "pci",
	[NVMF_ADDR_FAMILY_IP4]		 = "ipv4",
	[NVMF_ADDR_FAMILY_IP6]		 = "ipv6",
	[NVMF_ADDR_FAMILY_IB]		 = "ib",
	[NVMF_ADDR_FAMILY_FC]		 = "fc",
};

const char *adrfam_str(u8 adrfam)
{
	return arg_str(adrfams, ARRAY_SIZE(adrfams), adrfam);
}

static const char * const subtypes[] = {
	[NVME_NQN_DISC]			 = "discovery subsystem",
	[NVME_NQN_NVME]			 = "nvme subsystem",
};

const char *subtype_str(u8 subtype)
{
	return arg_str(subtypes, ARRAY_SIZE(subtypes), subtype);
}

static const char * const treqs[] = {
	[NVMF_TREQ_NOT_SPECIFIED]	 = "not specified",
	[NVMF_TREQ_REQUIRED]		 = "required",
	[NVMF_TREQ_NOT_REQUIRED]	 = "not required",
};

const char *treq_str(u8 treq)
{
	return arg_str(treqs, ARRAY_SIZE(treqs), treq);
}

static const char * const prtypes[] = {
	[NVMF_RDMA_PRTYPE_NOT_SPECIFIED] = "not specified",
	[NVMF_RDMA_PRTYPE_IB]		 = "infiniband",
	[NVMF_RDMA_PRTYPE_ROCE]		 = "roce",
	[NVMF_RDMA_PRTYPE_ROCEV2]	 = "roce-v2",
	[NVMF_RDMA_PRTYPE_IWARP]	 = "iwarp",
};

const char *prtype_str(u8 prtype)
{
	return arg_str(prtypes, ARRAY_SIZE(prtypes), prtype);
}

static const char * const qptypes[] = {
	[NVMF_RDMA_QPTYPE_CONNECTED]	 = "connected",
	[NVMF_RDMA_QPTYPE_DATAGRAM]	 = "datagram",
};

const char *qptype_str(u8 qptype)
{
	return arg_str(qptypes, ARRAY_SIZE(qptypes), qptype);
}

static const char * const cms[] = {
	[NVMF_RDMA_CMS_RDMA_CM]		 = "rdma-cm",
};

const char *cms_str(u8 cm)
{
	return arg_str(cms, ARRAY_SIZE(cms), cm);
}

u8 to_trtype(char *str)
{
	if (strcmp(str, TRTYPE_STR_RDMA) == 0)
		return NVMF_TRTYPE_RDMA;
	if (strcmp(str, TRTYPE_STR_FC) == 0)
		return NVMF_TRTYPE_FC;
	if (strcmp(str, TRTYPE_STR_TCP) == 0)
		return NVMF_TRTYPE_TCP;
	return 0;
}

u8 to_adrfam(char *str)
{
	if (strcmp(str, ADRFAM_STR_IPV4) == 0)
		return NVMF_ADDR_FAMILY_IP4;
	if (strcmp(str, ADRFAM_STR_IPV6) == 0)
		return NVMF_ADDR_FAMILY_IP6;
	if (strcmp(str, ADRFAM_STR_FC) == 0)
		return NVMF_ADDR_FAMILY_FC;
	return 0;
}

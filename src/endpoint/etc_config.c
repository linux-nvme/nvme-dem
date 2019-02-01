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

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "common.h"
#include "tags.h"

static void addr_to_ipv4(__u8 *addr, char *str)
{
	int			 i, n;

	addr += IPV4_OFFSET;
	for (i = 0; i < IPV4_LEN; i++, addr++, str += n)
		n = sprintf(str, "%s%u", i ? "." : "", *addr);
}

static void addr_to_ipv6(__u8 *_addr, char *str)
{
	__u16			*addr;
	int			 i, n;

	_addr += IPV6_OFFSET;
	addr = (__u16 *) _addr;

	for (i = 0; i < IPV6_LEN; i++, addr++, str += n)
		n = sprintf(str, "%s%x", i ? IPV6_DELIM : "",
			    htons(*addr));
}

static void addr_to_fc(__u8 *addr, char *str)
{
	int			 i, n;

	addr += FC_OFFSET;
	for (i = 0; i < FC_LEN; i++, addr++, str += n)
		n = sprintf(str, "%s%u", i ? FC_DELIM : "", *addr);
}

static void read_dem_config(FILE *fd, struct portid *iface)
{
	int			 ret;
	char			 tag[LARGEST_TAG + 1];
	char			 val[LARGEST_VAL + 1];

	ret = parse_line(fd, tag, LARGEST_TAG, val, LARGEST_VAL);
	if (ret)
		return;

	if (strcasecmp(tag, TAG_TYPE) == 0)
		strncpy(iface->type, val, CONFIG_TYPE_SIZE);
	else if (strcasecmp(tag, TAG_FAMILY) == 0)
		strncpy(iface->family, val, CONFIG_FAMILY_SIZE);
	else if (strcasecmp(tag, TAG_ADDRESS) == 0)
		strncpy(iface->address, val, CONFIG_ADDRESS_SIZE);
}

int enumerate_interfaces(void)
{
	struct dirent		*entry;
	DIR			*dir;
	FILE			*fd;
	char			 config_file[FILENAME_MAX + 1];
	struct portid		*iface;
	int			 cnt = 0;
	int			 adrfam = 0;
	int			 ret;

	dir = opendir(PATH_NVMF_DEM_DISC);
	if (unlikely(!dir))
		return -errno;

	for_each_dir(entry, dir) {
		if ((strcmp(entry->d_name, CONFIG_FILENAME) == 0) ||
		    (strcmp(entry->d_name, SIGNATURE_FILE_FILENAME) == 0))
			continue;

		snprintf(config_file, FILENAME_MAX, "%s%s",
			 PATH_NVMF_DEM_DISC, entry->d_name);

		fd = fopen(config_file, "r");
		if (fd == NULL)
			continue;

		iface = malloc(sizeof(*iface));
		if (!iface) {
			fclose(fd);
			cnt = -ENOMEM;
			free_interfaces();
			goto out2;
		}

		memset(iface, 0, sizeof(*iface));

		while (!feof(fd))
			read_dem_config(fd, iface);

		fclose(fd);

		if (!valid_trtype(iface->type)) {
			print_info("Invalid trtype");
			free(iface);
			continue;
		}

		adrfam = set_adrfam(iface->family);
		if (!adrfam) {
			print_info("Invalid adrfam");
			free(iface);
			continue;
		}

		switch (adrfam) {
		case NVMF_ADDR_FAMILY_IP4:
			ret = ipv4_to_addr(iface->address, iface->addr);
			break;
		case NVMF_ADDR_FAMILY_IP6:
			ret = ipv6_to_addr(iface->address, iface->addr);
			break;
		case NVMF_ADDR_FAMILY_FC:
			ret = fc_to_addr(iface->address, iface->addr);
			break;
		default:
			ret = -EINVAL;
		}

		if (ret < 0) {
			print_err("ignored invalid traddr %s", iface->address);
			free(iface);
			continue;
		}

		print_debug("adding interface for %s %s %s",
			    iface->type, iface->family, iface->address);

		list_add_tail(&iface->node, interfaces);
		cnt++;
	}
out2:
	return cnt;
}

void free_interfaces(void)
{
	struct linked_list	*p;
	struct linked_list	*n;
	struct portid		*iface;

	list_for_each_safe(p, n, interfaces) {
		list_del(p);
		iface = container_of(p, struct portid, node);
		free(iface);
	}
}

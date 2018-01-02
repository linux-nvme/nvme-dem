/*
 * NVMe over Fabrics Distributed Endpoint Manager (NVMe-oF DEM).
 * Copyright (c) 2017-2018 Intel Corporation, Inc. All rights reserved.
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
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include "common.h"
#include "tags.h"

#define CFS_PATH		"/sys/kernel/config/nvmet/"
#define CFS_SUBSYS		"subsystems/"
#define CFS_NS			"namespaces/"
#define CFS_DEV_PATH		"device_path"
#define CFS_ALLOW_ANY		"attr_allow_any_host"
#define CFS_ENABLE		"enable"
#define CFS_PORTS		"ports/"
#define CFS_TR_ADRFAM		"addr_adrfam"
#define CFS_TR_TYPE		"addr_trtype"
#define CFS_TREQ		"addr_treq"
#define CFS_TR_ADDR		"addr_traddr"
#define CFS_TR_SVCID		"addr_trsvcid"
#define CFS_HOSTS		"hosts/"
#define CFS_ALLOWED		"allowed_hosts/"

#define SYSFS_PATH		"/sys/class/nvme"
#define SYSFS_PREFIX		"nvme"
#define SYSFS_PREFIX_LEN	4
#define SYSFS_DEVICE		SYSFS_PREFIX "%dn%d"
#define SYSFS_TRANSPORT		"transport"
#define SYSFS_PCIE		"pcie"
#define NULL_DEVICE		"/dev/nullb0"
#define NVME_DEVICE		"/dev/" SYSFS_DEVICE

#define TRUE			'1'
#define FALSE			'0'

#define REQUIRED		"required"
#define NOT_REQUIRED		"not required"
#define NOT_SPECIFIED		"not specified"

#define MAXPATHLEN		512

#define for_each_dir(entry, subdir)			\
	while ((entry = readdir(subdir)) != NULL)	\
		if (strcmp(entry->d_name, ".") &&	\
		    strcmp(entry->d_name, ".."))
#define write_chr(fn, ch)				\
	do {						\
		fd = fopen(fn, "w");			\
		if (fd) {				\
			fputc(ch, fd);			\
			fclose(fd);			\
		}					\
	} while (0)
#define write_str(fn, s)				\
	do {						\
		fd = fopen(fn, "w");			\
		if (fd) {				\
			fputs(s, fd);			\
			fclose(fd);			\
		}					\
	} while (0)

// cd /sys/kernel/config/nvmet/subsystems/
// echo creating ${SUBSYSTEM} NSID ${NSID} for device ${DEV}
// [ -e ${SUBSYSTEM} ] || mkdir ${SUBSYSTEM}
// echo -n 1 > ${SUBSYSTEM}/attr_allow_any_host
int create_subsys(char *subsys, int allowany)
{
	char			dir[MAXPATHLEN];
	FILE			*fd;
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return -errno;

	ret = mkdir(subsys, 0x755);
	if (ret && errno != EEXIST)
		goto err;

	ret = chdir(subsys);
	if (ret)
		goto err;

	write_chr(CFS_ALLOW_ANY, allowany ? TRUE : FALSE);
	goto out;
err:
	ret = -errno;
out:
	chdir(dir);
	return ret;
}

static void delete_all_allowed_hosts(void)
{
	char			path[MAXPATHLEN];
	DIR			*dir;
	struct dirent		*entry;

	dir = opendir(CFS_ALLOWED);
	if (dir) {
		for_each_dir(entry, dir) {
			sprintf(path, CFS_ALLOWED "%s", entry->d_name);
			remove(path);
		}
		closedir(dir);
	}

}

static void delete_all_ns(void)
{
	char			path[MAXPATHLEN];
	FILE			*fd;
	DIR			*dir;
	struct dirent		*entry;

	dir = opendir(CFS_NS);
	if (dir) {
		for_each_dir(entry, dir) {
			sprintf(path, CFS_NS "%s", entry->d_name);
			chdir(path);
			write_chr(CFS_ENABLE, FALSE);
			chdir("../..");
			rmdir(path);
		}
		closedir(dir);
	}
}

static void unlink_all_ports(char *subsys)
{
	char			path[MAXPATHLEN];
	DIR			*dir;
	DIR			*portdir;
	struct dirent		*entry;
	struct dirent		*portentry;

	chdir(CFS_PATH);

	dir = opendir(CFS_PORTS);
	if (dir) {
		for_each_dir(entry, dir) {
			sprintf(path, CFS_PORTS "%s/" CFS_SUBSYS,
				entry->d_name);
			portdir = opendir(path);
			for_each_dir(portentry, portdir)
				if (!strcmp(portentry->d_name, subsys)) {
					sprintf(path,
						CFS_PORTS "%s/" CFS_SUBSYS "%s",
						entry->d_name, subsys);
					remove(path);
				}
		}
		closedir(dir);
	}
}

// cd /sys/kernel/config/nvmet/subsystems/
// rm ${SUBSYSTEM}/hosts/*
// echo 0 > ${SUBSYSTEM}/namespaces/*/enable
// rmdir ${SUBSYSTEM}/namespaces/*
// rm ../ports/*/subsystems/${SUBSYSTEM}
// rmdir ${SUBSYSTEM}
int delete_subsys(char *subsys)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	int			 ret;

	getcwd(dir, sizeof(dir));

	sprintf(path, CFS_PATH CFS_SUBSYS "%s", subsys);
	ret = chdir(path);
	if (ret)
		return -errno;

	delete_all_allowed_hosts();
	delete_all_ns();
	unlink_all_ports(subsys);

	chdir(CFS_PATH CFS_SUBSYS);
	rmdir(subsys);

	chdir(dir);
	return 0;
}

// cd /sys/kernel/config/nvmet/subsystems/
// mkdir ${SUBSYSTEM}/namespaces/${NSID}
// echo -n ${DEV} > ${SUBSYSTEM}/namespaces/${NSID}/device_path
// echo -n 1 > ${SUBSYSTEM}/namespaces/${NSID}/enable
int create_ns(char *subsys, int nsid, int devid, int devnsid)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	FILE			*fd;
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return -errno;

	sprintf(path, "%s/" CFS_NS "%d", subsys, nsid);
	ret = mkdir(path, 0x755);
	if (ret && errno != EEXIST)
		goto err;

	ret = chdir(path);
	if (ret)
		goto err;

	if (devid < 0)
		write_str(CFS_DEV_PATH, NULL_DEVICE);
	else {
		sprintf(path, NVME_DEVICE, devid, devnsid);
		write_str(CFS_DEV_PATH, path);
	}

	write_chr(CFS_ENABLE, TRUE);
	goto out;
err:
	ret = -errno;
out:
	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/subsystems/
// echo -n 0 > ${SUBSYSTEM}/namespaces/${NSID}/enable
// rmdir ${SUBSYSTEM}/namespaces/${NSID}
int delete_ns(char *subsys, int nsid)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	FILE			*fd;
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return -errno;

	sprintf(path, "%s/" CFS_NS "%d", subsys, nsid);
	ret = chdir(path);
	if (ret)
		goto err;

	write_chr(CFS_ENABLE, FALSE);

	chdir("../../..");
	rmdir(path);
	goto out;
err:
	ret = -errno;
out:
	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/hosts
// mkdir ${HOSTNQN}
int create_host(char *host)
{
	char			 dir[MAXPATHLEN];
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_HOSTS);
	if (ret)
		return -errno;

	ret = mkdir(host, 0x755);
	if (ret && errno != EEXIST)
		ret = -errno;

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/hosts
// rmdir ${HOSTNQN}
int delete_host(char *host)
{
	char			 dir[MAXPATHLEN];
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_HOSTS);
	if (ret)
		return -errno;

	ret = rmdir(host);
	if (ret)
		ret = -errno;

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/ports
// mkdir ${NVME_PORT}
// echo -n ipv4 > ${NVME_PORT}/addr_adrfam
// echo -n rdma > ${NVME_PORT}/addr_trtype
// echo -n not required > ${NVME_PORT}/addr_treq
// echo -n ${TARGET} > ${NVME_PORT}/addr_traddr
// echo -n ${PORT} > ${NVME_PORT}/addr_trsvcid
int create_portid(int portid, char *fam, char *typ, int req, char *addr,
		  int svcid)
{
	char			 dir[MAXPATHLEN];
	char			 str[8];
	FILE			*fd;
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_PORTS);
	if (ret)
		return -errno;

	snprintf(str, sizeof(str) - 1, "%d", portid);
	ret = mkdir(str, 0x755);
	if (ret && errno != EEXIST)
		goto err;

	ret = chdir(str);
	if (ret)
		goto err;

	write_str(CFS_TR_ADRFAM, fam);
	write_str(CFS_TR_TYPE, typ);
	write_str(CFS_TR_ADDR, addr);
	if (req == NVMF_TREQ_REQUIRED)
		write_str(CFS_TREQ, REQUIRED);
	else if (req == NVMF_TREQ_NOT_REQUIRED)
		write_str(CFS_TREQ, NOT_REQUIRED);
	else
		write_str(CFS_TREQ, NOT_SPECIFIED);

	snprintf(str, sizeof(str) - 1, "%d", svcid);
	write_str(CFS_TR_SVCID, str);

	goto out;
err:
	ret = -errno;
out:
	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/ports
// rm -f ${NVME_PORT}/subsystems/*
// rmdir ${NVME_PORT}
int delete_portid(int portid)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	DIR			*subdir;
	struct dirent		*entry;
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_PORTS);
	if (ret)
		return -errno;

	snprintf(path, sizeof(path) - 1, "%d/" CFS_SUBSYS, portid);

	subdir = opendir(path);
	if (!subdir) {
		ret = -errno;
		goto out;
	}
	for_each_dir(entry, subdir)
		remove(entry->d_name);
	closedir(subdir);

	snprintf(path, sizeof(path) - 1, "%d", portid);
	rmdir(path);
out:
	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/subsystems/
// ln -s ../../hosts/${HOSTNQN} ${SUBSYSTEM}/hosts/${HOSTNQN}
int link_host_to_subsys(char *subsys, char *host)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	char			 link[MAXPATHLEN];
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return -errno;

	sprintf(path, CFS_PATH CFS_HOSTS "%s", host);
	sprintf(link, "%s/" CFS_ALLOWED "%s", subsys, host);

	ret = symlink(path, link);
	if (ret)
		ret = -errno;

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/subsystems
// rm ${SUBSYSTEM}/hosts/{HOSTNQN}
int unlink_host_from_subsys(char *subsys, char *host)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return -errno;

	sprintf(path, "%s/" CFS_ALLOWED "%s", subsys, host);
	ret = remove(path);
	if (ret)
		ret = -errno;

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/ports
// ln -s ../subsystems/${SUBSYSTEM} ${NVME_PORT}/subsystems/${SUBSYSTEM}
int link_port_to_subsys(char *subsys, int portid)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	char			 link[MAXPATHLEN];
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_PORTS);
	if (ret)
		return -errno;

	sprintf(path, CFS_PATH CFS_SUBSYS "%s", subsys);
	sprintf(link, "%d/" CFS_SUBSYS "%s", portid, subsys);

	ret = symlink(path, link);
	if (ret)
		ret = -errno;

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/ports
// rm ${NVME_PORT}/subsystems/${SUBSYSTEM}
int unlink_port_from_subsys(char *subsys, int portid)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_PORTS);
	if (ret)
		return -errno;

	sprintf(path, "%d/" CFS_SUBSYS "%s", portid, subsys);
	ret = remove(path);
	if (ret)
		ret = -errno;

	chdir(dir);
	return ret;
}

void free_devices(void)
{
	struct list_head	*p;
	struct list_head	*n;
	struct nsdev		*dev;

	list_for_each_safe(p, n, devices) {
		list_del(p);
		dev = container_of(p, struct nsdev, node);
		free(dev);
	}
}

int enumerate_devices(void)
{
	char			 dir[MAXPATHLEN];
	char			 path[MAXPATHLEN];
	char			 buf[5];
	DIR			*subdir;
	DIR			*nvmedir;
	struct dirent		*entry;
	struct dirent		*subentry;
	struct nsdev		*device;
	FILE			*fd;
	int			 cnt = 0;
	int			 ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(SYSFS_PATH);
	if (ret)
		return -errno;

	nvmedir = opendir(".");
	for_each_dir(entry, nvmedir) {
		sprintf(path, "%s/" SYSFS_TRANSPORT, entry->d_name);
		fd = fopen(path, "r");
		if (!fd)
			continue;

		fgets(buf, sizeof(buf), fd);
		if (strncmp(SYSFS_PCIE, buf, sizeof(buf)) == 0) {
			subdir = opendir(entry->d_name);
			for_each_dir(subentry, subdir)
				if (strncmp(subentry->d_name, SYSFS_PREFIX,
					    SYSFS_PREFIX_LEN) == 0) {
					device = malloc(sizeof(*device));
					if (!device) {
						ret = -ENOMEM;
						free_devices();
						goto out;
					}
					sscanf(subentry->d_name, SYSFS_DEVICE,
					       &device->devid, &device->nsid);
					list_add_tail(&device->node, devices);
					cnt++;
					devices++;
				}
			closedir(subdir);
		}
		fclose(fd);
	}
	closedir(nvmedir);

	fd = fopen(NULL_DEVICE, "r");
	if (fd) {
		device = malloc(sizeof(*device));
		if (!device) {
			ret = -ENOMEM;
			free_devices();
			goto out;
		}
		device->devid = device->nsid = -1;
		list_add_tail(&device->node, devices);
		cnt++;
		fclose(fd);
	}
out:
	chdir(dir);
	return cnt;
}

void free_interfaces(void)
{
	struct list_head	*p;
	struct list_head	*n;
	struct port_id		*interface;

	list_for_each_safe(p, n, interfaces) {
		list_del(p);
		interface = container_of(p, struct port_id, node);
		free(interface);
	}
}

static int find_interface(struct fi_info *prov)
{
	struct list_head	*p;
	struct list_head	*n;
	struct port_id		*interface;

	list_for_each_safe(p, n, interfaces) {
		interface = container_of(p, struct port_id, node);
		if (interface->prov->addr_format == prov->addr_format &&
		    interface->prov->src_addrlen == prov->src_addrlen &&
		    memcmp(interface->prov->src_addr, prov->src_addr,
			   prov->src_addrlen) == 0)
			return 1;
	}
	return 0;
}

static void addr_to_ipv4(u8 *addr, char *str)
{
	int			 i, n;

	addr += IPV4_OFFSET;
	for (i = 0; i < IPV4_LEN; i++, addr++, str += n)
		n = sprintf(str, "%s%u", i ? "." : "", *addr);
}

static void addr_to_ipv6(u8 *_addr, char *str)
{
	u16			*addr;
	int			 i, n;

	_addr += IPV6_OFFSET;
	addr = (u16 *) _addr;

	for (i = 0; i < IPV6_LEN; i++, addr++, str += n)
		n = sprintf(str, "%s%x", i ? IPV6_DELIM : "",
			    htons(*addr));
}

static void addr_to_fc(u8 *addr, char *str)
{
	int			 i, n;

	addr += FC_OFFSET;
	for (i = 0; i < FC_LEN; i++, addr++, str += n)
		n = sprintf(str, "%s%u", i ? FC_DELIM : "", *addr);
}

static void store_interface_address(struct port_id *interface,
				    struct fi_info *p)
{
	if (p->addr_format == FI_SOCKADDR_IN) {
		strcpy(interface->family, ADRFAM_STR_IPV4);
		strcpy(interface->type, TRTYPE_STR_RDMA);
		addr_to_ipv4(p->src_addr, interface->address);
	} else if (p->addr_format == FI_SOCKADDR_IN6) {
		strcpy(interface->family, ADRFAM_STR_IPV6);
		strcpy(interface->type, TRTYPE_STR_RDMA);
		addr_to_ipv6(p->src_addr, interface->address);
	} else {
		strcpy(interface->family, ADRFAM_STR_FC);
		strcpy(interface->type, TRTYPE_STR_FC);
		addr_to_fc(p->src_addr, interface->address);
	}
}

int enumerate_interfaces(void)
{
	struct fi_info		*hints;
	struct fi_info		*prov;
	struct fi_info		*p;
	struct port_id		*interface;
	char			*ib_name = "ib";
	int			 ret;
	int			 cnt = 0;
	u8			 ipv4_loopback[] = {
		0x02, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8			 ipv6_loopback[] = {
		0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00 };

	hints = fi_allocinfo();
	if (!hints) {
		print_err("no memory for hints");
		return -ENOMEM;
	}

	ret = fi_getinfo(FI_VER, NULL, NULL, 0, hints, &prov);
	if (ret) {
		cnt = ret;
		goto out1;
	}

	for (p = prov; p; p = p->next)
		if (p->src_addrlen && !find_interface(p)) {
			if (strncmp(p->domain_attr->name, ib_name,
				    strlen(ib_name)))
				continue;
			if (p->addr_format == FI_SOCKADDR_IN &&
			    memcmp(ipv4_loopback, p->src_addr,
				   sizeof(ipv4_loopback)) == 0)
				continue;
			if (p->addr_format == FI_SOCKADDR_IN6 &&
			    memcmp(ipv6_loopback, p->src_addr,
				   sizeof(ipv6_loopback)) == 0)
				continue;
			interface = malloc(sizeof(*interface));
			if (!interface) {
				cnt = -ENOMEM;
				free_interfaces();
				goto out2;
			}
			interface->prov = p;
			store_interface_address(interface, p);
			print_debug("adding interface for %s %s %s",
				    interface->type, interface->family,
				    interface->address);
			list_add_tail(&interface->node, interfaces);
			cnt++;
		}
out2:
	fi_freeinfo(prov);
out1:
	fi_freeinfo(hints);
	return cnt;
}

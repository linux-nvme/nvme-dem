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
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "common.h"
#include "tags.h"

#define CFS_PATH	"/sys/kernel/config/nvmet/"
#define CFS_SUBSYS	"subsystems/"
#define CFS_NS		"namespaces/"
#define CFS_DEV_PATH	"device_path"
#define CFS_ALLOW_ANY	"attr_allow_any_host"
#define CFS_ENABLE	"enable"
#define CFS_PORTS	"ports/"
#define CFS_TR_ADRFAM	"addr_adrfam"
#define CFS_TR_TYPE	"addr_trtype"
#define CFS_TREQ	"addr_treq"
#define CFS_TR_ADDR	"addr_traddr"
#define CFS_TR_SVCID	"addr_trsvcid"
#define CFS_HOSTS	"hosts/"
#define CFS_ALLOWED	"allowed_hosts/"

#define TRUE		'1'
#define FALSE		'0'

#define MAXPATHLEN	512

#define IS_NOT_DOT_DIR(entry) \
	(strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
#define for_each_dir(entry, subdir) \
	while ((entry = readdir(subdir)) != NULL) \
		if (IS_NOT_DOT_DIR(entry))
#define write_chr(fn, ch)		\
	do {				\
		fd = fopen(fn, "w");	\
		if (fd) {		\
			fputc(ch, fd);	\
			fclose(fd);	\
		}			\
	} while (0)
#define write_str(fn, s)		\
	do {				\
		fd = fopen(fn, "w");	\
		if (fd) {		\
			fputs(s, fd);	\
			fclose(fd);	\
		}			\
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
		return ret;

	ret = mkdir(subsys, 0x755);
	if (ret)
		goto out;

	ret = chdir(subsys);
	if (ret)
		goto out;

	write_chr(CFS_ALLOW_ANY, allowany ? TRUE : FALSE);
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
	char			dir[MAXPATHLEN];
	char			path[MAXPATHLEN];
	int			ret;

	getcwd(dir, sizeof(dir));

	sprintf(path, CFS_PATH CFS_SUBSYS "%s", subsys);
	ret = chdir(path);
	if (ret)
		return ret;

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
	char			dir[MAXPATHLEN];
	char			path[MAXPATHLEN];
	FILE			*fd;
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return ret;

	sprintf(path, "%s/" CFS_NS "%d", subsys, nsid);
	ret = mkdir(path, 0x755);
	if (ret)
		goto out;

	ret = chdir(path);
	if (ret)
		goto out;

	if (devid < 0)
		write_str(CFS_DEV_PATH, "/dev/nullb0");
	else {
		sprintf(path, "/dev/nvme%dn%d", devid, devnsid);
		write_str(CFS_DEV_PATH, path);
	}

	write_chr(CFS_ENABLE, TRUE);
out:
	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/subsystems/
// echo -n 0 > ${SUBSYSTEM}/namespaces/${NSID}/enable
// rmdir ${SUBSYSTEM}/namespaces/${NSID}
int delete_ns(char *subsys, int nsid)
{
	char			dir[MAXPATHLEN];
	char			path[MAXPATHLEN];
	FILE			*fd;
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return ret;

	sprintf(path, "%s/" CFS_NS "%d", subsys, nsid);
	ret = chdir(path);
	if (ret)
		goto out;

	write_chr(CFS_ENABLE, FALSE);

	chdir("../../..");
	rmdir(path);
out:
	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/hosts
// mkdir ${HOSTNQN}
int create_host(char *host)
{
	char			dir[MAXPATHLEN];
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_HOSTS);
	if (ret)
		return ret;

	ret = mkdir(host, 0x755);

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/hosts
// rmdir ${HOSTNQN}
int delete_host(char *host)
{
	char			dir[MAXPATHLEN];
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_HOSTS);
	if (ret)
		return ret;

	rmdir(host);

	chdir(dir);
	return 0;
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
	char			dir[MAXPATHLEN];
	char			str[8];
	FILE			*fd;
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_PORTS);
	if (ret)
		return ret;

	snprintf(str, sizeof(str) - 1, "%d", portid);
	ret = mkdir(str, 0x755);
	if (ret)
		goto out;

	ret = chdir(str);
	if (ret)
		goto out;

	write_str(CFS_TR_ADRFAM, fam);
	write_str(CFS_TR_TYPE, typ);
	write_str(CFS_TR_ADDR, addr);
	write_chr(CFS_TREQ, req ? TRUE : FALSE);

	snprintf(str, sizeof(str) - 1, "%d", svcid);
	write_str(CFS_TR_SVCID, str);
out:
	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/ports
// rm -f ${NVME_PORT}/subsystems/*
// rmdir ${NVME_PORT}
int delete_portid(int portid)
{
	char			dir[MAXPATHLEN];
	char			path[MAXPATHLEN];
	DIR			*subdir;
	struct dirent		*entry;
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_PORTS);
	if (ret)
		return ret;

	snprintf(path, sizeof(path) - 1, "%d/" CFS_SUBSYS, portid);

	subdir = opendir(path);
	for_each_dir(entry, subdir)
		remove(entry->d_name);

	snprintf(path, sizeof(path) - 1, "%d", portid);
	rmdir(path);

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/subsystems/
// ln -s ../../hosts/${HOSTNQN} ${SUBSYSTEM}/hosts/${HOSTNQN}
int link_host_to_subsys(char *subsys, char *host)
{
	char			dir[MAXPATHLEN];
	char			path[MAXPATHLEN];
	char			link[MAXPATHLEN];
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return ret;

	sprintf(path, CFS_PATH CFS_HOSTS "%s", host);
	sprintf(link, "%s/" CFS_ALLOWED "%s", subsys, host);

	ret = symlink(path, link);

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/subsystems
// rm ${SUBSYSTEM}/hosts/{HOSTNQN}
int unlink_host_to_subsys(char *subsys, char *host)
{
	char			dir[MAXPATHLEN];
	char			path[MAXPATHLEN];
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_SUBSYS);
	if (ret)
		return ret;

	sprintf(path, "%s/" CFS_ALLOWED "%s", subsys, host);
	ret = remove(path);

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/ports
// ln -s ../subsystems/${SUBSYSTEM} ${NVME_PORT}/subsystems/${SUBSYSTEM}
int link_port_to_subsys(char *subsys, int portid)
{
	char			dir[MAXPATHLEN];
	char			path[MAXPATHLEN];
	char			link[MAXPATHLEN];
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_PORTS);
	if (ret)
		return ret;

	sprintf(path, CFS_PATH CFS_SUBSYS "%s", subsys);
	sprintf(link, "%d/" CFS_SUBSYS "%s", portid, subsys);

	ret = symlink(path, link);

	chdir(dir);
	return ret;
}

// cd /sys/kernel/config/nvmet/ports
// rm ${NVME_PORT}/subsystems/${SUBSYSTEM}
int unlink_port_to_subsys(char *subsys, int portid)
{
	char			dir[MAXPATHLEN];
	char			path[MAXPATHLEN];
	int			ret;

	getcwd(dir, sizeof(dir));

	ret = chdir(CFS_PATH CFS_PORTS);
	if (ret)
		return ret;

	sprintf(path, "%d/" CFS_SUBSYS "%s", portid, subsys);
	ret = remove(path);

	chdir(dir);
	return ret;
}

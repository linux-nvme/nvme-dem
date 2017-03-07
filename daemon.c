/*
 * Distributed Endpoint Manager.
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

#include "mongoose.h"
#include <pthread.h>
#include <dirent.h>

#include "common.h"

#define PATH_NVMF_DEM_DISC	"/etc/nvme/nvmeof-dem/"
#define NUM_CONFIG_ITEMS	3

/*For setting server options - e.g., SSL, document root, ...*/
static struct mg_serve_http_opts s_http_server_opts;

static void *json_ctx;
static int s_sig_num;
static int poll_timeout = 100;

/*
 *  trtypes
 *	[NVMF_TRTYPE_RDMA]	= "rdma",
 *	[NVMF_TRTYPE_FC]	= "fibre-channel",
 *	[NVMF_TRTYPE_LOOP]	= "loop",
 *
 *  adrfam
 *	[NVMF_ADDR_FAMILY_IP4]	= "ipv4",
 *	[NVMF_ADDR_FAMILY_IP6]	= "ipv6",
 *	[NVMF_ADDR_FAMILY_IB]	= "infiniband",
 *	[NVMF_ADDR_FAMILY_FC]	= "fibre-channel",
 */

/*HACK*/
#define CONFIG_TYPE_SIZE	8
#define CONFIG_FAMILY_SIZE	8
#define CONFIG_ADDRESS_SIZE	64
#define CONFIG_MAX_LINE		256

struct  dem_interface {
	char trtype[CONFIG_TYPE_SIZE + 1];
	char addrfam[CONFIG_FAMILY_SIZE + 1];
	char hostaddr[CONFIG_ADDRESS_SIZE + 1];
};

int count_dem_config_files()
{
	struct dirent *entry;
	DIR *dir;
	int filecount = 0;
	dir = opendir(PATH_NVMF_DEM_DISC);
	if (dir != NULL) {
		while ((entry = readdir(dir))) {
			if (!strcmp(entry->d_name,"."))
				continue;
			if (!strcmp(entry->d_name,".."))
				continue;
			filecount++;
		}
	} else {
		printf("%s does not exist\n", PATH_NVMF_DEM_DISC);
		filecount = -ENOENT;
	}

	closedir(dir);

	printf("Found %d files\n", filecount);

	return filecount;
}

int read_dem_config_files(struct dem_interface *iface)
{
	struct dirent *entry;
	DIR *dir;
	int ret = 0;
	char config_file[FILENAME_MAX+1];
	FILE *fid;
	int count = 0;

	dir = opendir(PATH_NVMF_DEM_DISC);
	while ((entry = readdir(dir))) {
		if (!strcmp(entry->d_name,"."))
			continue;
		if (!strcmp(entry->d_name,".."))
			continue;
		snprintf(config_file, FILENAME_MAX, "%s%s",
			 PATH_NVMF_DEM_DISC, entry->d_name);

		printf("path = %s\n", config_file);
		if ((fid = fopen(config_file,"r")) != NULL){
			char buf[CONFIG_MAX_LINE];
			char *str;
			int configinfo = 0;

			printf("Opening %s\n",config_file);
			while (1) {
				fgets(buf, CONFIG_MAX_LINE, fid);

				if (feof(fid))
					break;

				if (buf[0] == '#') //skip comments
					continue;

				str = strtok(buf, "= \t");
				if (!strcmp(str, "Type")) {
					str = strtok(NULL, " \n\t");
					strncpy(iface[count].trtype, str,
						CONFIG_TYPE_SIZE);
					configinfo++;
					printf("%s %s\n", config_file,
					       iface[count].trtype);
					continue;
				}
				if (!strcmp(str, "Family")) {
					str = strtok(NULL, " \n\t");
					strncpy(iface[count].addrfam, str,
						CONFIG_FAMILY_SIZE);
					configinfo++;
					printf("%s %s\n",config_file,
					       iface[count].addrfam);
					continue;
				}
				if (!strcmp(str, "Address")) {
					str = strtok(NULL, " \n\t");
					strncpy(iface[count].hostaddr, str,
						CONFIG_ADDRESS_SIZE);
					configinfo++;
					printf("%s %s\n",config_file,
					       iface[count].hostaddr);
					continue;
				}
			}
			fclose(fid);

			if (configinfo != NUM_CONFIG_ITEMS)
				fprintf(stderr, "%s: bad config file."
					" Ignoring interface.\n", config_file);
			else
				count++;
		} else {
			fprintf(stderr, "Failed to open config file %s\n",
				config_file);
			ret = -ENOENT;
			goto out;
		}
	}
out:
	closedir(dir);
	return ret;
}

void shutdown_dem()
{
	s_sig_num = 1;
}

static void signal_handler(int sig_num)
{
	signal(sig_num, signal_handler);
	s_sig_num = sig_num;
	printf("\n");
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		handle_http_request(json_ctx, c, ev_data);
		break;
	case MG_EV_HTTP_CHUNK:
	case MG_EV_ACCEPT:
	case MG_EV_CLOSE:
	case MG_EV_POLL:
	case MG_EV_SEND:
	case MG_EV_RECV:
		break;
	default:
		fprintf(stderr, "ev_handler: Unexpected request %d\n", ev);
	}
}

static void *poll_loop(struct mg_mgr *mgr)
{
	s_sig_num = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	while (s_sig_num == 0)
		mg_mgr_poll(mgr, poll_timeout);

	mg_mgr_free(mgr);

	return NULL;
}

static void *xport_loop(void *p)
{
	if (p != NULL) p = NULL;  //TEMP

	return NULL;
}

static int daemonize(void)
{
	pid_t pid, sid;

	if (getuid() != 0) {
		fprintf(stderr, "Must be root to run dem as a daemon\n");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork failed %d\n", pid);
		return pid;
	}

	if (pid) /* if parent, exit to allow child to run as daemon */
		exit(0);

	umask(0022);

	sid = setsid();
	if (sid < 0) {
		fprintf(stderr, "setsid failed %d\n", sid);
		return sid;
	}

	if ((chdir("/")) < 0) {
		fprintf(stderr, "could not change dir to /\n");
		return -1;
	}

	freopen("/var/log/dem_debug.log", "a", stdout);
	freopen("/var/log/dem.log", "a", stderr);

	return 0;
}

int main(int argc, char *argv[])
{
	struct mg_mgr		 mgr;
	struct mg_connection	*c;
	struct mg_bind_opts	 bind_opts;
	int			 opt;
	const char		*err_str;
	char			*cp;
	char			*s_http_port = "12345";
#if MG_ENABLE_SSL
	const char		*ssl_cert = NULL;
#endif
	pthread_attr_t		pthread_attr;
	pthread_t		xport_pthread;
	int			ret;
	int			run_as_daemon = 0;
	int			filecount;
	struct dem_interface    *interfaces;

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto help;

	mg_mgr_init(&mgr, NULL);

	s_http_server_opts.document_root = NULL;

	/* Process CLI options for HTTP server */
	while ((opt = getopt(argc, argv, "dp:s:r:")) != -1) {
		switch (opt) {
		case 'd':
			run_as_daemon = 1;
			break;
		case 'r':
			s_http_server_opts.document_root = optarg;
			break;
		case 'p':
			s_http_port = optarg;
			fprintf(stderr, "Using port %s\n", s_http_port);
			break;
		case 's':
#if MG_ENABLE_SSL
			ssl_cert = optarg;
#endif
			break;
		default:
help:
			fprintf(stderr, "Usage: %s %s\n", argv[0],
				"{-r <root>} {-p <port>} {-s <ssl_cert>}\n");
			return 1;
		}
	}

	if (run_as_daemon) {
		ret = daemonize();
		if (ret)
			return 1;
	}

	/* Use current binary directory as document root */
	if (!s_http_server_opts.document_root) {
		cp = strrchr(argv[0], DIRSEP);
		if (cp != NULL) {
			*cp = '\0';
			s_http_server_opts.document_root = argv[0];
		}
	}

	/* Set HTTP server options */
	memset(&bind_opts, 0, sizeof(bind_opts));
	bind_opts.error_string = &err_str;

#if MG_ENABLE_SSL
	if (ssl_cert != NULL)
		bind_opts.ssl_cert = ssl_cert;
#endif
	c = mg_bind_opt(&mgr, s_http_port, ev_handler, bind_opts);
	if (c == NULL) {
		fprintf(stderr, "Error starting server on port %s: %s\n",
			s_http_port, *bind_opts.error_string);
		return 1;
	}

	mg_set_protocol_http_websocket(c);

	filecount = count_dem_config_files();
	if (filecount < 0)
		return filecount;

	interfaces = calloc(filecount, sizeof (struct dem_interface));
	ret = read_dem_config_files(interfaces);
	if (ret)
		return 1;

	json_ctx = init_json("config.json");
	if (!json_ctx)
		return 1;

	printf("Starting server on port %s, serving %s\n",
	       s_http_port, s_http_server_opts.document_root);
	fflush(stdout);

	s_sig_num = 0;

	signal(SIGSEGV, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	pthread_attr_init(&pthread_attr);

	/*TODO: change to pass interfaces and filecount<?> */
	if (pthread_create(&xport_pthread, &pthread_attr, xport_loop, NULL)) {
		fprintf(stderr, "Error starting transport thread\n");
		return 1;
	}

	poll_loop(&mgr);

	pthread_kill(xport_pthread, s_sig_num);

	free(interfaces);

	cleanup_json(json_ctx);

	return 0;
}

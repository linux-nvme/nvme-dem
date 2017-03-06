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
#include <unistd.h>

#include "common.h"

#define PATH_NVMF_DEM_DISC          "/etc/nvme/nvmeof-dem/"
#define NUM_CONFIG_ITEMS            3

/*For setting server options - e.g., SSL, document root, ...*/
static struct mg_serve_http_opts s_http_server_opts;

static void *json_ctx;
static int s_sig_num;
static int poll_timeout = 100;

/*
trtypes
        [NVMF_TRTYPE_RDMA]      = "rdma",
        [NVMF_TRTYPE_FC]        = "fibre-channel",
        [NVMF_TRTYPE_LOOP]      = "loop",
 
adrfam
        [NVMF_ADDR_FAMILY_IP4]  = "ipv4",
        [NVMF_ADDR_FAMILY_IP6]  = "ipv6",
        [NVMF_ADDR_FAMILY_IB]   = "infiniband",
        [NVMF_ADDR_FAMILY_FC]   = "fibre-channel",
*/
 
/*HACK*/
struct  dem_interface {
        char trtype[13];
        char addrfam[13];
        char hostaddr[15];
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
 
        printf("dealing with %d files\n", filecount);
 
        return filecount;
}

int read_dem_config_files(struct dem_interface *interface)
{
        struct dirent *entry;
        DIR *dir;
        int ret = 0;
        char config_file[1024];
        FILE *fid;
        int count = 0;
 
        dir = opendir(PATH_NVMF_DEM_DISC);
        while ((entry = readdir(dir))) {
                if (!strcmp(entry->d_name,"."))
                        continue;
                if (!strcmp(entry->d_name,".."))
                        continue;

                snprintf(config_file, sizeof(config_file), "%s%s",
			 PATH_NVMF_DEM_DISC, entry->d_name);
 
                printf("path = %s\n", config_file);
                if((fid = fopen(config_file,"r")) != NULL){
                        char buf[100];
                        char *str;
                        int configinfo = 0;
 
                        printf("Opening %s\n",config_file);
                        while (1) {
                                fgets(buf, 100, fid);
 
                                if (feof(fid))
                                        break;
 
                                if (buf[0] == '#') //skip comments
                                        continue;

                                str = strtok(buf, "=,\t");
                                        if(!strcmp(str, "Type")) {
                                                str = strtok(NULL, "=");
                                                strcpy(interface[count].trtype, str);
                                                configinfo++;
                                                printf("%s %s\n", config_file,
						       interface[count].trtype);
                                                continue;
                                        }
                                        if(!strcmp(str, "Family")) {
                                                str = strtok(NULL, "=");
                                                strcpy(interface[count].addrfam, str);
                                                configinfo++;
                                                printf("%s %s\n",config_file,
						       interface[count].addrfam);
                                                continue;
                                        }
                                        if(!strcmp(str, "Address")) {
                                                str = strtok(NULL, "=");
                                                strcpy(interface[count].hostaddr, str);
                                                configinfo++;
                                                printf("%s %s\n",config_file,
						       interface[count].hostaddr);
                                                continue;
                                        }
                        }
                        fclose(fid);
 
                        if (configinfo != NUM_CONFIG_ITEMS)
                                fprintf(stderr, "%s: bad config file. Igoring interface.\n",
					config_file);
                        else
                                count++;
                } else {
                        fprintf(stderr, "failed to open config file %s\n", config_file);
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

static void *poll_loop(void *p)
{
	struct mg_mgr *mgr = p;

	s_sig_num = 0;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	while (s_sig_num == 0)
		mg_mgr_poll(mgr, poll_timeout);

	mg_mgr_free(mgr);

	return NULL;
}

static void *transport_loop(void *p)
{
        if (p != NULL) p = NULL;  //TEMP
 
        return NULL;
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
        pthread_attr_t          pthread_attr;
        pthread_t               poll_pthread;
        pthread_t               transport_pthread;
        int                     ret;
        int                     filecount;
        struct dem_interface    *interfaces;

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto help;

	mg_mgr_init(&mgr, NULL);

	s_http_server_opts.document_root = NULL;

	/* Process CLI options for HTTP server */
	while ((opt = getopt(argc, argv, "p:s:r:")) != -1) {
		switch (opt) {
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
			exit(1);
		}
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
		exit(1);
	}

	mg_set_protocol_http_websocket(c);
 
        filecount = count_dem_config_files();
        if (filecount < 0)
                return filecount;
 
        interfaces = calloc(filecount, sizeof (struct dem_interface));
        ret = read_dem_config_files(interfaces);
        if (ret)
                exit(1);
 
	json_ctx = init_json("config.json");
	if (!json_ctx)
		return 1;

	printf("Starting server on port %s, serving %s\n",
		s_http_port, s_http_server_opts.document_root);

        s_sig_num = 0;
 
        signal(SIGSEGV, signal_handler);
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
 
        pthread_attr_init(&pthread_attr);

        pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&poll_pthread, &pthread_attr, poll_loop, &mgr)) {
                fprintf(stderr, "Error starting poll thread\n");
                exit(1);
        }
 
        /*TODO: change to pass interfaces and filecount<?> rather than "mgr"*/
        if (pthread_create(&transport_pthread, &pthread_attr, transport_loop, &mgr)) {
                fprintf(stderr, "Error starting transport thread\n");
                exit(1);
        }
 
        while (s_sig_num == 0)
                usleep(200);

	usleep(500);

        pthread_kill(poll_pthread, s_sig_num);
        pthread_kill(transport_pthread, s_sig_num);
        free(interfaces);

	cleanup_json(json_ctx);

	return 0;
}

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
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "common.h"

/* needs to be < NVMF_DISC_KATO in connect AND < 2 MIN for upstream target */
#define KEEP_ALIVE_TIMER	100000 /* ms */

// TODO disable DEV_DEBUG before pushing to gitlab
#if 1
#define DEV_DEBUG
#endif

int				 stopped;
int				 signalled;
int				 debug;

void shutdown_dem(void)
{
	stopped = 1;
}

static void signal_handler(int sig_num)
{
	signalled = sig_num;

	shutdown_dem();
}

static int daemonize(void)
{
	pid_t			 pid, sid;

	if (getuid() != 0) {
		print_err("must be root to run demd as a daemon");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		print_err("fork failed %d", pid);
		return pid;
	}

	if (pid) /* if parent, exit to allow child to run as daemon */
		exit(0);

	umask(0022);

	sid = setsid();
	if (sid < 0) {
		print_err("setsid failed %d", sid);
		return sid;
	}

	if ((chdir("/")) < 0) {
		print_err("could not change dir to /");
		return -1;
	}

	freopen("/var/log/dem-ac_debug.log", "a", stdout);
	freopen("/var/log/dem-ac.log", "a", stderr);

	return 0;
}

static int init_dem(int argc, char *argv[])
{
	int			 opt;
	int			 run_as_daemon;
#ifdef DEV_DEBUG
	const char		*opt_list = "?qd";
	const char		*arg_list = "{-q} {-d}\n"
		"-q - quite mode, no debug prints\n"
		"-d - run as a daemon process (default is standalone)\n";
#else
	const char		*opt_list = "?ds";
	const char		*arg_list = "{-d} {-s}\n"
		"-d - enable debug prints in log files\n"
		"-s - run as a standalone process (default is daemon)\n";
#endif

	if (argc > 1 && strcmp(argv[1], "--help") == 0)
		goto help;

#ifdef DEV_DEBUG
	debug = 1;
	run_as_daemon = 0;
#else
	debug = 0;
	run_as_daemon = 1;
#endif

	/* Process CLI options for HTTP server */
	while ((opt = getopt(argc, argv, opt_list)) != -1) {
		switch (opt) {
#ifdef DEV_DEBUG
		case 'q':
			debug = 0;
			break;
		case 'd':
			run_as_daemon = 1;
			break;
#else
		case 'd':
			debug = 0;
			break;
		case 's':
			run_as_daemon = 1;
			break;
#endif
		case '?':
		default:
help:
			print_info("Usage: %s %s", argv[0], arg_list);
			return 1;
		}
	}

	if (run_as_daemon) {
		if (daemonize())
			return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int			 ret = 1;

	if (init_dem(argc, argv))
		goto out;

	signalled = stopped = 0;

	print_info("Starting server");

	while (!stopped)
		usleep(500);

	if (signalled)
		printf("\n");

	ret = 0;
out:
	return ret;
}

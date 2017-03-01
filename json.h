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

void *init_json(char *filename);
void store_config_file(void *context);
void cleanup_json(void *context);
int add_ctrl(void *context, char *p);
int del_ctrl(void *context, char *p);
int list_ctrl(void *context, char *response);
int rename_ctrl(void *context, char *old, char *new);
int set_ctrl(void *context, char *alias, char *type, char *family,
		char *address, int port, int refresh);
int rename_ss(void *context, char *alias, char *old, char *new);
int show_ctrl(void *context, char *alias, char *response);
int add_host(void *context, char *nqn);
int del_host(void *context, char *nqn);
int list_host(void *context, char *response);
int rename_host(void *context, char *old, char *new);
int show_host(void *context, char *nqn, char *response);
int add_subsys(void *context, char *alias, char *ss);
int del_subsys(void *context, char *alias, char *ss);
int add_acl(void *context, char *nqn, char *ss);
int del_acl(void *context, char *nqn,  char *ss);

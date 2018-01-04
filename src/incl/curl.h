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

int init_curl(int debug);
void cleanup_curl(void);
int exec_get(char *url, char **result);
int exec_delete(char *url);
int exec_delete_ex(char *url, char *data, int len);
int exec_put(char *url, char *data, int len);
int exec_post(char *url, char *data, int len);
int exec_patch(char *url, char *data, int len);

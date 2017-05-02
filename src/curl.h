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

extern int formatted;

void *init_curl();
void cleanup_curl(void *p);
int exec_get(void *p, char *url, char **result);
int exec_delete(void *p, char *url);
int exec_put(void *p, char *url, char *data, int len);
int exec_post(void *p, char *url, char *data, int len);


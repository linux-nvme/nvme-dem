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

void show_target_data(json_t *parent);
void show_target_list(json_t *parent, int indent);
void show_host_data(json_t *parent);
void show_host_list(json_t *parent, int indent);
void show_group_data(json_t *parent);
void show_group_list(json_t *parent);
void show_config(json_t *parent);
void show_usage_data(json_t *parent);

#ifndef UNUSED
#define UNUSED(x) ((void) x)
#endif

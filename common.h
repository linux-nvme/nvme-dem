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

void shutdown_dem(void);
void handle_http_request(void *json_ctx, struct mg_connection *c,
			 void *ev_data);

void *init_json(char *filename);
void cleanup_json(void *context);

extern int debug;

#define print_debug(f, x...) do { \
	if (debug) { \
		printf("%s(%d) " f "\n", __func__, __LINE__, ##x); \
		fflush(stdout); \
	}} while (0)
#define print_info(f, x...) do { \
	printf(f "\n", ##x); \
	fflush(stdout); \
	} while (0)
#define print_err(f, x...) do { \
	fprintf(stderr, "%s(%d) Error: " f "\n", __func__, __LINE__, ##x); \
	fflush(stderr); \
	} while (0)

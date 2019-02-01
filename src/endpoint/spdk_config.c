// SPDX-License-Identifier: DUAL GPL-2.0/BSD
/*
 * NVMe over Fabrics Distributed Endpoint Management (NVMe-oF DEM).
 * Copyright (c) 2017-2019 Intel Corporation, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pciaccess.h>

#include "common.h"
#include "tags.h"

#include "spdk/stdinc.h"
#include "spdk/event.h"
#include "spdk/nvme.h"
#include "spdk/jsonrpc.h"

#define DEV_PREFIX		"NVMe"
#define NULL_BLK_DEVICE		"nullb0"
#define BDEV_TYPE_PCIE		"PCIe"

#define BDEV_FMT		DEV_PREFIX "%d"
#define NVME_DEV_FMT		DEV_PREFIX "%dn%d"
#define PCIE_ADDR_FMT		"%04x:%02x:%02x.%d"

#define MAXSTRLEN		64

#define SPDK_NQN_MIN_SIZE	15
#define SPDK_NQN_FMT		"%3s.%4d-%2d.%s"

#define PCI_CLASS_STORAGE_NVME	0x010802

#define NULL_BLK_SIZE		512
#define NULL_NUM_BLKS		512

//HACK - need access to the recv buffer do handle erros
#define SPDK_JSONRPC_MAX_VALUES	1024
struct spdk_jsonrpc_client {
	int sockfd;

	struct spdk_json_val values[SPDK_JSONRPC_MAX_VALUES];
	size_t recv_buf_size;
	uint8_t *recv_buf;

	spdk_jsonrpc_client_response_parser parser_fn;
	void *parser_ctx;
};

static int			nvme_count;

static struct spdk_jsonrpc_client *client;

static struct linked_list	portid_list;
static struct linked_list	subsys_list;

static inline bool valid_nqn(char *nqn)
{
	int			 len = strlen(nqn);
	int			 cnt;
	char			 part1[4];
	int			 part2;
	int			 part3;
	char			 part4[MAX_NQN_SIZE];

	if (len < SPDK_NQN_MIN_SIZE || len > MAX_NQN_SIZE)
		return false;

	cnt = sscanf(nqn, SPDK_NQN_FMT, part1, &part2, &part3, part4);
	if (cnt != 4)
		return false;

	if (strcmp(part1, "nqn"))
		return false;

	if (part2 == 0 || part3 == 0 || part3 > 12)
		return false;

	return index(part4, ':') != NULL;
}

static inline struct subsystem *find_subsys(char *nqn)
{
	struct subsystem	*subsys;

	list_for_each_entry(subsys, &subsys_list, node)
		if (!strcmp(subsys->nqn, nqn))
			return subsys;
	return NULL;
}

static inline struct host *find_host(struct subsystem *subsys, char *nqn)
{
	struct host		*host;

	list_for_each_entry(host, &subsys->host_list, node)
		if (!strcmp(host->nqn, nqn))
			return host;
	return NULL;
}

static inline struct portid *find_portid(int id)
{
	struct portid		*portid;

	list_for_each_entry(portid, &portid_list, node)
		if (portid->portid == id)
			return portid;
	return NULL;
}

static inline struct _portid *find_portid_link(struct subsystem *subsys,
					       struct portid *portid)
{
	struct _portid		*_portid;

	list_for_each_entry(_portid, &subsys->portid_list, node)
		if (_portid->portid == portid)
			return _portid;
	return NULL;
}

static int resp_parser(void *parser_ctx, const struct spdk_json_val *result)
{
	bool			*val = (bool *) parser_ctx;
	char			 res[MAXSTRLEN + 1];

	*val = false;

	if (result->type == SPDK_JSON_VAL_TRUE)
		*val = true;
	else if (result->type == SPDK_JSON_VAL_NUMBER) {
		snprintf(res, MAXSTRLEN, "%*s",
			 result->len, (char *) result->start);
		*(int *) parser_ctx = atoi(res);
	}

	return 0;
}

/* TODO Modified from spdk - feedback suggestion:
 *   - prevent segv if *s is null
 */
static int my_decode_string(const struct spdk_json_val *val, void *out)
{
	char			**s = out;

	if (*s)
		free(*s);

	if (val->type != SPDK_JSON_VAL_STRING &&
	    val->type != SPDK_JSON_VAL_NAME)
		return -EINVAL;

	*s = spdk_json_strdup(val);

	return !*s;
}

/* TODO Modified from spdk - feedback suggestion:
 *   - remove "found" for forward compatability
 */
static int my_decode_object(const struct spdk_json_val *values,
			    const struct spdk_json_object_decoder *decoders,
			    size_t num_decoders, void *out)
{
	uint32_t		 i;
	bool			 invalid = false;
	size_t			 decidx;
	bool			*seen;

	if (values == NULL || values->type != SPDK_JSON_VAL_OBJECT_BEGIN)
		return -1;

	seen = calloc(sizeof(bool), num_decoders);
	if (seen == NULL)
		return -1;

	for (i = 0; i < values->len;) {
		const struct spdk_json_val *name = &values[i + 1];
		const struct spdk_json_val *v = &values[i + 2];

		for (decidx = 0; decidx < num_decoders; decidx++) {
			const struct spdk_json_object_decoder *dec;

			dec = &decoders[decidx];
			if (spdk_json_strequal(name, dec->name)) {
				void *field = (void *)
					((uintptr_t) out + dec->offset);

				seen[decidx] = true;
				invalid = dec->decode_func(v, field);
				break;
			}
		}

		i += 1 + spdk_json_val_len(v);
	}

	for (decidx = 0; decidx < num_decoders; decidx++) {
		if (!decoders[decidx].optional & !seen[decidx]) {
			/* required field is missing */
			invalid = true;
			break;
		}
	}

	free(seen);
	return invalid ? -1 : 0;
}

static int parse_err_resp(void)
{
	int			 i, rc = -EINVAL;
	int			 len = client->recv_buf_size;
	char			*p = (char *) client->recv_buf;
	char			*start = NULL;

	for (i = 0; i < len && p && *p != '\n'; i++, p++) {
		if (!strncmp(p, "code", 4)) {
			rc = atoi(p + 6);
			p += 6;
			continue;
		}
		if (!strncmp(p, "message", 7)) {
			p += 10;
			start = p;
			continue;
		}
		if (start && *p == '"') {
			print_err("spdk_json error '%.*s'",
				  (int) (p - start), start);
			break;
		}
	}

	return rc;
}

static int _delete_subsys(struct subsystem *subsys)
{
	int			 rc;
	bool			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req, "delete_nvmf_subsystem", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "nqn", subsys->nqn);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp ? 0 : -ENOENT;
}

static int _create_subsys(struct subsystem *subsys)
{
	int			 rc;
	bool			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req, "nvmf_subsystem_create", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "nqn", subsys->nqn);
	spdk_json_write_named_bool(w, "allow_any_host", subsys->allowany);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp ? 0 : -ENOENT;
}

static int spdk_delete_subsys(char *nqn)
{
	struct subsystem	*subsys;
	struct host		*host, *h;
	struct _portid		*portid, *p;

	subsys = find_subsys(nqn);
	if (!subsys)
		return -ENOENT;

	_delete_subsys(subsys);

	list_del(&subsys->node);

	list_for_each_entry_safe(portid, p, &subsys->portid_list, node)
		free(portid);

	list_for_each_entry_safe(host, h, &subsys->host_list, node)
		free(host);

	free(subsys);

	return 0;
}

static int spdk_create_subsys(char *subnqn, int allowany)
{
	struct subsystem	*subsys;

	if (!valid_nqn(subnqn))
		return -EINVAL;

	if (find_subsys(subnqn))
		return -EEXIST;

	subsys = malloc(sizeof(*subsys));
	if (!subsys)
		return -ENOMEM;

	memset(subsys, 0, sizeof(*subsys));

	INIT_LINKED_LIST(&subsys->portid_list);
	INIT_LINKED_LIST(&subsys->host_list);

	strcpy(subsys->nqn, subnqn);
	subsys->allowany = !!allowany;

	list_add(&subsys->node, &subsys_list);

	return _create_subsys(subsys);
}

static int spdk_create_ns(char *subnqn, int nsid, int devid, int devnsid)
{
	int			 rc;
	int			 resp;
	char			 bdev_name[MAXSTRLEN];
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	if (!find_subsys(subnqn))
		return -ENOENT;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	if (devid == NULLB_DEVID)
		strcpy(bdev_name, NULL_BLK_DEVICE);
	else
		sprintf(bdev_name, NVME_DEV_FMT, devid, devnsid);

	w = spdk_jsonrpc_begin_request(req, "nvmf_subsystem_add_ns", 1);

	spdk_json_write_named_object_begin(w, "params");

	spdk_json_write_named_string(w, "nqn", subnqn);

	spdk_json_write_named_object_begin(w, "namespace");
	spdk_json_write_named_uint32(w, "nsid", nsid);
	spdk_json_write_named_string(w, "bdev_name", bdev_name);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return (resp == nsid) ? 0 : -EINVAL;
}

static int spdk_delete_ns(char *subnqn, int nsid)
{
	int			 rc;
	bool			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	if (!find_subsys(subnqn))
		return -ENOENT;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req, "nvmf_subsystem_remove_ns", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "nqn", subnqn);
	spdk_json_write_named_uint32(w, "nsid", nsid);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp ? 0 : -ENOENT;
}

static int spdk_create_host(char *host)
{
	UNUSED(host);

	/* SPDK does not support independent hosts */
	return 0;
}

static int spdk_delete_host(char *host)
{
	UNUSED(host);

	/* SPDK does not support independent hosts */
	return 0;
}

static int spdk_create_portid(int id, char *fam, char *typ, int req,
			      char *addr, int trsvcid)
{
	struct portid		*portid;

	UNUSED(req);

	/* SPDK does not support independent port ids */

	if (find_portid(id))
		return -EEXIST;

	portid = malloc(sizeof(*portid));
	if (!portid)
		return -ENOMEM;

	memset(portid, 0, sizeof(*portid));

	portid->portid = id;

	strcpy(portid->family, fam);
	strcpy(portid->type, typ);
	strcpy(portid->address, addr);

	sprintf(portid->port, "%d", trsvcid);

	list_add(&portid->node, &portid_list);

	return 0;
}

static int add_listener(struct subsystem *subsys, struct portid *portid)
{
	int			 rc;
	bool			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req, "nvmf_subsystem_add_listener", 1);

	spdk_json_write_named_object_begin(w, "params");

	spdk_json_write_named_string(w, "nqn", subsys->nqn);

	spdk_json_write_named_object_begin(w, "listen_address");
	spdk_json_write_named_string(w, "trtype", portid->type);
	spdk_json_write_named_string(w, "adrfam", portid->family);
	spdk_json_write_named_string(w, "traddr", portid->address);
	spdk_json_write_named_string(w, "trsvcid", portid->port);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp ? 0 : -ENOENT;
}

static int remove_listener(struct subsystem *subsys, struct portid *portid)
{
	int			 rc;
	bool			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req,
				       "nvmf_subsystem_remove_listener", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "nqn", subsys->nqn);

	spdk_json_write_named_object_begin(w, "listen_address");
	spdk_json_write_named_string(w, "trtype", portid->type);
	spdk_json_write_named_string(w, "traddr", portid->address);
	spdk_json_write_named_string(w, "adrfam", portid->family);
	spdk_json_write_named_string(w, "trsvcid", portid->port);
	spdk_json_write_object_end(w);

	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp ? 0 : -ENOENT;
}

static int add_host(struct subsystem *subsys, struct host *host)
{
	int			 rc;
	bool			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req, "nvmf_subsystem_add_host", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "nqn", subsys->nqn);
	spdk_json_write_named_string(w, "host", host->nqn);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp ? 0 : -ENOENT;
}

static int remove_host(struct subsystem *subsys, struct host *host)
{
	int			rc;
	bool			resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req, "nvmf_subsystem_remove_host", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "nqn", subsys->nqn);
	spdk_json_write_named_string(w, "host", host->nqn);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp ? 0 : -ENOENT;
}

static int spdk_link_host_to_subsys(char *subnqn, char *hostnqn)
{
	int			 rc;
	bool			 reconnect;
	struct subsystem	*subsys;
	struct host		*host;
	struct _portid		*_portid;

	if (!valid_nqn(hostnqn))
		return -EINVAL;

	subsys = find_subsys(subnqn);
	if (!subsys)
		return -ENOENT;

	host = find_host(subsys, hostnqn);
	if (host)
		return 0;

	host = malloc(sizeof(*host));
	if (!host)
		return -ENOMEM;

	memset(host, 0, sizeof(*host));

	strncpy(host->nqn, hostnqn, MAX_NQN_SIZE);

	reconnect = list_empty(&subsys->host_list);
	if (reconnect)
		list_for_each_entry(_portid, &subsys->portid_list, node)
			remove_listener(subsys, _portid->portid);

	rc = add_host(subsys, host);
	if (rc) {
		free(host);
		return rc;
	}

	list_add_tail(&host->node, &subsys->host_list);

	if (reconnect) {
		list_for_each_entry(_portid, &subsys->portid_list, node)
			add_listener(subsys, _portid->portid);
		usleep(100);	/* allow spdk to finish starting the listeners */
	}

	return 0;
}

static int spdk_unlink_host_from_subsys(char *subnqn, char *hostnqn)
{
	int			 rc;
	struct subsystem	*subsys;
	struct host		*host;

	subsys = find_subsys(subnqn);
	if (!subsys)
		return -ENOENT;

	host = find_host(subsys, hostnqn);
	if (!host)
		return 0;

	rc = remove_host(subsys, host);

	list_del(&host->node);
	free(host);

	return rc;
}

static int spdk_link_port_to_subsys(char *nqn, int id)
{
	int			 rc;
	struct portid		*portid;
	struct _portid		*_portid;
	struct subsystem	*subsys;

	portid = find_portid(id);
	if (!portid)
		return -EINVAL;

	subsys = find_subsys(nqn);
	if (!subsys)
		return -EINVAL;

	if (find_portid_link(subsys, portid))
		return 0;

	_portid = malloc(sizeof(*_portid));
	if (!_portid)
		return -ENOMEM;

	rc = add_listener(subsys, portid);
	if (rc) {
		free(_portid);
		return rc;
	}

	_portid->portid = portid;

	list_add(&_portid->node, &subsys->portid_list);

	return 0;
}

static int spdk_unlink_port_from_subsys(char *nqn, int id)
{
	int			 rc;
	struct portid		*portid;
	struct _portid		*_portid;
	struct subsystem	*subsys;

	portid = find_portid(id);
	if (!portid)
		return -ENOENT;

	subsys = find_subsys(nqn);
	if (!subsys)
		return -ENOENT;

	rc = remove_listener(subsys, portid);
	if (rc)
		return rc;

	_portid = find_portid_link(subsys, portid);
	if (_portid) {
		list_del(&_portid->node);
		free(_portid);
	}

	return 0;
}

static void _delete_portid(struct portid *portid)
{
	struct subsystem	*subsys;

	list_for_each_entry(subsys, &subsys_list, node)
		spdk_unlink_port_from_subsys(subsys->nqn, portid->portid);

	list_del(&portid->node);
	free(portid);
}

static int spdk_delete_portid(int id)
{
	struct portid		*portid;

	portid = find_portid(id);
	if (portid)
		_delete_portid(portid);

	return 0;
}

static int create_device(char *bdev_name, const char *traddr)
{
	int			 cnt;
	struct nsdev		*device;

	UNUSED(traddr);

	device = malloc(sizeof(*device));
	if (!device) {
		free_devices();
		return -ENOMEM;
	}

	if (!strcasecmp(bdev_name, NULL_BLK_DEVICE)) {
		device->devid = NULLB_DEVID;
		device->nsid = 0;
	} else {
		cnt = sscanf(bdev_name, NVME_DEV_FMT,
			     &device->devid, &device->nsid);
		if (cnt != 2) {
			free_devices();
			free(device);
			return -EINVAL;
		}
	}

	print_debug("adding device %s", bdev_name);

	list_add_tail(&device->node, devices);

	return 0;
}

struct bdev {
	char			*name;
};

const struct spdk_json_object_decoder bdev_decoders[] = {
	{"name", offsetof(struct bdev, name), my_decode_string, false},
};

static int bdev_parse(const struct spdk_json_val *val, void *out)
{
	return my_decode_object(val, bdev_decoders,
				NUM_ENTRIES(bdev_decoders), out);
}

static int bdev_parser(void *out, const struct spdk_json_val *val)
{
	int			 i, rc;
	int			 num_bdevs = val->len;
	struct bdev		*bdevs;

	if (val->type != SPDK_JSON_VAL_ARRAY_BEGIN)
		return -EINVAL;

	bdevs = calloc(num_bdevs, sizeof(struct bdev));
	if (!bdevs)
		return -ENOMEM;

	rc = spdk_json_decode_array(val, bdev_parse, bdevs, num_bdevs, out,
				    sizeof(struct bdev));
	if (rc)
		goto out;

	num_bdevs = *(int *) out;

	for (i = 0; i < num_bdevs; i++) {
		create_device(bdevs[i].name, NULL);
		free(bdevs[i].name);
	}
out:
	free(bdevs);
	return rc;
}

static int spdk_enumerate_devices(void)
{
	int			 rc;
	int			 num_bdevs;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return 0;

	w = spdk_jsonrpc_begin_request(req, "get_bdevs", 1);
	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, bdev_parser, &num_bdevs);
	if (rc)
		return parse_err_resp();

	return num_bdevs;
}

static int add_nvme_device(struct pci_device *dev)
{
	int			 rc;
	bool			 resp;
	char			 name[MAXSTRLEN];
	char			 address[MAXSTRLEN];
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	sprintf(name, BDEV_FMT, nvme_count++);

	sprintf(address, PCIE_ADDR_FMT,
		dev->domain, dev->bus, dev->dev, dev->func);

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req, "construct_nvme_bdev", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "name", name);
	spdk_json_write_named_string(w, "trtype", BDEV_TYPE_PCIE);
	spdk_json_write_named_string(w, "traddr", address);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp;
}

static int add_null_device(void)
{
	int			 rc;
	bool			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return -ENOMEM;

	w = spdk_jsonrpc_begin_request(req, "construct_null_bdev", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "name", NULL_BLK_DEVICE);
	spdk_json_write_named_uint32(w, "block_size", NULL_BLK_SIZE);
	spdk_json_write_named_uint64(w, "num_blocks", NULL_NUM_BLKS);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
	if (rc)
		return parse_err_resp();

	return resp;
}

static void _del_nvme_device(char *name)
{
	int			resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return;
	if (!strcmp(name, NULL_BLK_DEVICE))
		w = spdk_jsonrpc_begin_request(req, "delete_null_bdev", 1);
	else
		w = spdk_jsonrpc_begin_request(req, "delete_bdev", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "name", name);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
}

static void del_nvme_device(void)
{
	char			name[MAXSTRLEN];

	sprintf(name, BDEV_FMT, --nvme_count);

	_del_nvme_device(name);
}

static int bdev_reset(void *out, const struct spdk_json_val *val)
{
	int			 i, rc;
	int			 num_bdevs = val->len;
	struct bdev		*bdevs;

	if (val->type != SPDK_JSON_VAL_ARRAY_BEGIN)
		return -EINVAL;

	bdevs = calloc(num_bdevs, sizeof(struct bdev));
	if (!bdevs)
		return -ENOMEM;

	rc = spdk_json_decode_array(val, bdev_parse, bdevs, num_bdevs, out,
				    sizeof(struct bdev));
	if (rc)
		goto out;

	num_bdevs = *(int *) out;

	for (i = 0; i < num_bdevs; i++) {
		_del_nvme_device(bdevs[i].name);
		free(bdevs[i].name);
	}
out:
	free(bdevs);
	return rc;
}

static void clear_devices(void)
{
	int			 rc;
	int			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return;

	w = spdk_jsonrpc_begin_request(req, "get_bdevs", 1);
	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, bdev_reset, &resp);
	if (rc)
		parse_err_resp();
}

static void del_null_device(void)
{
	int			resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return;

	w = spdk_jsonrpc_begin_request(req, "delete_null_bdev", 1);

	spdk_json_write_named_object_begin(w, "params");
	spdk_json_write_named_string(w, "name", NULL_BLK_DEVICE);
	spdk_json_write_object_end(w);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	spdk_jsonrpc_client_recv_response(client, resp_parser, &resp);
}

static int enumerate_nvme_devices(void)
{
	struct pci_device_iterator *iter;
	struct pci_device	*dev;
	int			 ret;

	ret = pci_system_init();
	if (ret != 0) {
		printf("Couldn't initialize PCI system\n");
		return ret;
	}

	iter = pci_slot_match_iterator_create(NULL);

	dev = pci_device_next(iter);
	while (dev) {
		if (dev->device_class == PCI_CLASS_STORAGE_NVME) {
			ret = add_nvme_device(dev);
			if (ret)
				break;
		}
		dev = pci_device_next(iter);
	}

	pci_system_cleanup();

	add_null_device();

	return 0;
}

struct namespc {
	int			 nsid;
};

struct subsys {
	char			*nqn;
	char			*subtype;
	struct namespc		*nsid;
};

const struct spdk_json_object_decoder ns_decoders[] = {
	{"nsid", offsetof(struct namespc, nsid), spdk_json_decode_int32, false},
};

static int _ns_parse(const struct spdk_json_val *val, void *out)
{
	return my_decode_object(val, ns_decoders,
				NUM_ENTRIES(ns_decoders), out);
}

static int ns_parse(const struct spdk_json_val *val, void *out)
{
	int			 i, rc;
	int			 cnt = val->len;
	struct namespc		*array;
	struct subsys		*subsys;

	if (val->type != SPDK_JSON_VAL_ARRAY_BEGIN)
		return -EINVAL;

	array = calloc(cnt, sizeof(struct namespc));
	if (!array)
		return -ENOMEM;

	rc = spdk_json_decode_array(val, _ns_parse, array, cnt, out,
				    sizeof(struct namespc));
	if (rc)
		goto out;

	cnt = *(int *) out;

	subsys = container_of(out, struct subsys, nsid);

	for (i = 0; i < cnt; i++)
		spdk_delete_ns(subsys->nqn, array[i].nsid);
out:
	free(array);
	return rc;
}

const struct spdk_json_object_decoder subsys_decoders[] = {
	{"nqn", offsetof(struct subsys, nqn), my_decode_string, false},
	{"subtype", offsetof(struct subsys, subtype), my_decode_string, false},
	{"namespaces", offsetof(struct subsys, nsid), ns_parse, true},
};

static int subsys_parse(const struct spdk_json_val *val, void *out)
{
	return my_decode_object(val, subsys_decoders,
				NUM_ENTRIES(subsys_decoders), out);
}

static int _clear_subsys(void *out, const struct spdk_json_val *val)
{
	int			 i, rc;
	int			 cnt = val->len;
	struct subsys		*array;

	if (val->type != SPDK_JSON_VAL_ARRAY_BEGIN)
		return -EINVAL;

	array = calloc(cnt, sizeof(struct subsys));
	if (!array)
		return -ENOMEM;

	rc = spdk_json_decode_array(val, subsys_parse, array, cnt, out,
				    sizeof(struct subsys));
	if (rc)
		goto out;

	cnt = *(int *) out;

	for (i = 0; i < cnt; i++) {
		if (!strcmp(array[i].subtype, "NVMe"))
			spdk_delete_subsys(array[i].nqn);
		free(array[i].nqn);
		free(array[i].subtype);
	}
out:
	free(array);
	return rc;
}

static void clear_subsystems(void)
{
	int			 rc;
	bool			 resp;
	struct spdk_json_write_ctx *w;
	struct spdk_jsonrpc_client_request *req;

	req = spdk_jsonrpc_client_create_request();
	if (req == NULL)
		return;

	w = spdk_jsonrpc_begin_request(req, "get_nvmf_subsystems", 1);

	spdk_jsonrpc_end_request(req, w);
	spdk_jsonrpc_client_send_request(client, req);

	spdk_jsonrpc_client_free_request(req);

	rc = spdk_jsonrpc_client_recv_response(client, _clear_subsys, &resp);
	if (rc)
		parse_err_resp();
}

static void spdk_reset_config(void)
{
	struct portid		*portid, *p;
	struct subsystem	*subsys, *s;

	list_for_each_entry_safe(portid, p, &portid_list, node)
		_delete_portid(portid);

	list_for_each_entry_safe(subsys, s, &subsys_list, node)
		spdk_delete_subsys(subsys->nqn);
}

static void reset_spdk_server(void)
{
	clear_subsystems();

	clear_devices();
}

static void spdk_stop_targets(void)
{
	spdk_reset_config();

	del_null_device();
	while (nvme_count)
		del_nvme_device();

	spdk_jsonrpc_client_close(client);
}

static int spdk_start_targets(void)
{
	INIT_LINKED_LIST(&portid_list);
	INIT_LINKED_LIST(&subsys_list);

	nvme_count = 0;

	client = spdk_jsonrpc_client_connect(SPDK_DEFAULT_RPC_ADDR, AF_UNIX);
	if (!client)
		return -ENOTCONN;

	reset_spdk_server();

	enumerate_nvme_devices();

	return 0;
}

static struct ops spdk_ops = {
	.delete_subsys			= spdk_delete_subsys,
	.create_subsys			= spdk_create_subsys,
	.create_ns			= spdk_create_ns,
	.delete_ns			= spdk_delete_ns,
	.create_host			= spdk_create_host,
	.delete_host			= spdk_delete_host,
	.create_portid			= spdk_create_portid,
	.delete_portid			= spdk_delete_portid,
	.link_host_to_subsys		= spdk_link_host_to_subsys,
	.unlink_host_from_subsys	= spdk_unlink_host_from_subsys,
	.link_port_to_subsys		= spdk_link_port_to_subsys,
	.unlink_port_from_subsys	= spdk_unlink_port_from_subsys,
	.enumerate_devices		= spdk_enumerate_devices,
	.reset_config			= spdk_reset_config,
	.start_targets			= spdk_start_targets,
	.stop_targets			= spdk_stop_targets,
};

struct ops *spdk_register_ops(void)
{
	return &spdk_ops;
}

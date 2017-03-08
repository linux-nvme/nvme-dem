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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#define is_whitespace(c) \
	(c == ' ' || c == '\t' || c == '\r')
#define is_quote(c) (c == '\'' || c == '"')
#define is_eol(c) (c == '\n' || c == EOF)
#define COMMENT '#'
#define EQUAL '='

int consume_line(FILE *fd)
{
	char c;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (is_eol(c))
			return 0;
	}
	return 1;
}

int consume_whitespace(FILE *fd)
{
	char c;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT) {
			return 1;
		}
		if (!is_whitespace(c) || is_eol(c)) {
			ungetc(c, fd);
			return 0;
		}
	}
	return 1;
}

int parse_word(FILE *fd, char *word, int n)
{
	char c, *p = word;
	int quoted = 0;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT && !quoted) {
			break;
		}
		if (is_quote(c)) {
			quoted = ~quoted;
			continue;
		}
		if (is_whitespace(c) || is_eol(c) || c == EQUAL) {
			ungetc(c, fd);
			break;
		}
		*p++ = c;
		if (--n == 0) {
			break;
		}
	}
	if (p == word)
		return 1;

	*p = 0;
	return 0;
}

int parse_equal(FILE *fd, char *word, int n)
{
	char c, *p = word;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT) {
			break;
		}
		if (c != EQUAL) {
			ungetc(c, fd);
			break;
		}
		*p++ = c;
		if (--n == 0) {
			break;
		}
	}
	if (p == word)
		return 1;

	*p = 0;
	return 0;
}

#define MAX_EQUAL 2

int parse_line(FILE *fd, char *tag, int tag_max, char *value, int value_max)
{
	char equal[MAX_EQUAL + 1];
	int ret;

	ret = consume_whitespace(fd);
	if (ret)
		goto out;
	ret = parse_word(fd, tag, tag_max);
	if (ret)
		goto out;
	ret = consume_whitespace(fd);
	if (ret)
		goto out;
	ret = parse_equal(fd, equal, MAX_EQUAL);
	if (ret)
		goto out;
	if (strcmp(equal, "=") != 0) {
		ret = -1;
		goto out;
	}
	ret = consume_whitespace(fd);
	if (ret)
		goto out;
	ret = parse_word(fd, value, value_max);
out:
	consume_line(fd);

	return ret;
}

int ipv4_to_addr(char *p, int *addr)
{
	char nibble[IPV4_WIDTH];
	int i, j, n, z;

	n = strlen(p) + 1;
	for (z = j = i = 0; i < n && j < IPV4_ADDR_LEN; i++, p++) {
		if (*p == '.' || *p == 0 || *p == '/') {
			if (z) {
				nibble[z] = 0;
				z = 0;
				addr[j] = atoi(nibble);
			} else
				addr[j] = 0;
			j++;
			if (*p == '/')
				break;
		} else
			nibble[z++] = *p;
	}

	for (; j < IPV4_ADDR_LEN; j++)
		addr[j] = 0;

	if (*p == '/')
		return atoi(++p);

	return 0;
}

int ipv6_to_addr(char *p, int *addr)
{
	char nibble[IPV6_WIDTH];
	int i, j, n, z;

	n = strlen(p) + 1;
	for (z = j = i = 0; i < n && j < IPV6_ADDR_LEN; i++, p++) {
		if (*p == ':' || *p == 0 || *p == '/') {
			if (z) {
				nibble[z] = 0;
				z = 0;
				sscanf(nibble, "%x", &addr[j]);
			} else
				addr[j] = 0;
			j++;
			if (*p == '/')
				break;
		} else
			nibble[z++] = *p;
	}

	for (; j < IPV6_ADDR_LEN; j++)
		addr[j] = 0;

	if (*p == '/')
		return atoi(++p);

	return 0;
}

void ipv4_mask(int *mask, int bits)
{
	int i, j;

	for (i = 0; i < IPV4_ADDR_LEN; i++) {
		mask[i] = 0;
		for (j = 0; j < IPV4_BITS; j++, bits--)
			mask[i] = (mask[i] * 2) + ((bits > 0) ? 1 : 0);
	}
}

void ipv6_mask(int *mask, int bits)
{
	int i, j;

	for (i = 0; i < IPV6_ADDR_LEN; i++) {
		mask[i] = 0;
		for (j = 0; j < IPV6_BITS; j++, bits--)
			mask[i] = (mask[i] * 2) + ((bits > 0) ? 1 : 0);
	}
}

int ipv4_equal(int *addr, int *dest, int *mask)
{
	int i;

	for (i = 0; i < IPV4_ADDR_LEN; i++)
		if ((addr[i] & mask[i]) != (dest[i] & mask[i]))
			return 0;

	return 1;
}

int ipv6_equal(int *addr, int *dest, int *mask)
{
	int i;

	for (i = 0; i < IPV6_ADDR_LEN; i++)
		if ((addr[i] & mask[i]) != (dest[i] & mask[i]))
			return 0;

	return 1;
}

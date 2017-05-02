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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#define is_whitespace(c) (c == ' ' || c == '\t' || c == '\r')
#define is_quote(c)	 (c == '\'' || c == '"')
#define is_eol(c)	 (c == '\n' || c == EOF)

#define COMMENT		'#'
#define EQUAL		'='
#define MAX_EQUAL	2

#define IPV4_LEN	4
#define IPV4_WIDTH	3
#define IPV4_DELIM	'.'

#define IPV6_LEN	8
#define IPV6_WIDTH	4
#define IPV6_DELIM	':'

#define FC_LEN		8
#define FC_WIDTH	2
#define FC_DELIM	':'

static int consume_line(FILE *fd)
{
	char			 c;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (is_eol(c))
			return 0;
	}
	return 1;
}

static int consume_whitespace(FILE *fd)
{
	char			 c;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT)
			return 1;

		if (!is_whitespace(c) || is_eol(c)) {
			ungetc(c, fd);
			return 0;
		}
	}
	return 1;
}

static int parse_word(FILE *fd, char *word, int n)
{
	char			 c, *p = word;
	int			 quoted = 0;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT && !quoted)
			break;

		if (is_quote(c)) {
			quoted = ~quoted;
			continue;
		}

		if (is_whitespace(c) || is_eol(c) || c == EQUAL) {
			ungetc(c, fd);
			break;
		}

		*p++ = c;

		if (--n == 0)
			break;
	}

	if (p == word)
		return 1;

	*p = 0;

	return 0;
}

static int parse_equal(FILE *fd, char *word, int n)
{
	char			 c, *p = word;

	while (!feof(fd)) {
		c = fgetc(fd);
		if (c == COMMENT)
			break;

		if (c != EQUAL) {
			ungetc(c, fd);
			break;
		}

		*p++ = c;

		if (--n == 0)
			break;
	}

	if (p == word)
		return 1;

	*p = 0;

	return 0;
}

int parse_line(FILE *fd, char *tag, int tag_max, char *value, int value_max)
{
	char			 equal[MAX_EQUAL + 1];
	int			 ret;

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

static int string_to_addr(char *p, int *addr, int len, int width, char delim)
{
	char			 nibble[8];
	int			 i, j, n, z;

	n = strlen(p) + 1;

	for (z = j = i = 0; i < n && j < len; i++, p++) {
		if (*p == delim || *p == 0 || *p == '/') {
			if (z) {
				if (z > width)
					return -1;
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

	for (; j < len; j++)
		addr[j] = 0;

	if (i < n && *p == '/')
		return atoi(++p);

	return 0;
}

int ipv4_to_addr(char *p, int *addr)
{
	return string_to_addr(p, addr, IPV4_LEN, IPV4_WIDTH, IPV4_DELIM);
}

int ipv6_to_addr(char *p, int *addr)
{
	return string_to_addr(p, addr, IPV6_LEN, IPV6_WIDTH, IPV6_DELIM);
}

int fc_to_addr(char *p, int *addr)
{
	return string_to_addr(p, addr, FC_LEN, FC_WIDTH, FC_DELIM);
}

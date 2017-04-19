/*
 * unit test module for address functions found in parse.c
 *
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

#define IPV4_LEN                4
#define IPV4_BITS               8
#define IPV4_WIDTH              3
#define IPV4_DELIM              '.'
#define IPV4_FORMAT             "%s%d"

#define IPV6_LEN                8
#define IPV6_BITS               16
#define IPV6_WIDTH              4
#define IPV6_DELIM              ':'
#define IPV6_FORMAT             "%s%x"

#define FC_LEN	                8
#define FC_BITS                 8
#define FC_WIDTH                2
#define FC_DELIM                ':'
#define FC_FORMAT               "%s%02x"

int string_to_addr(char *p, int *addr, int len, int width, char delim)
{
	char nibble[8];
	int bits;
	int i, j, n, z;

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

	if (*p == '/')
		return atoi(++p);

	return 0;
}

void print_addr(int *addr, int len, char delim, char *format)
{
	int i;
	char d[2] = { delim, 0 };

	for (i = 0; i < len; i++)
		printf(format, i ? d : "", addr[i]);
	printf("\n");
}

void addr_mask(int *mask, int bits, int len, int width)
{
	int i, j;

	for (i = 0; i < len; i++) {
		mask[i] = 0;
		for (j = 0; j < width; j++, bits--)
			mask[i] = (mask[i] * 2) + ((bits > 0) ? 1 : 0);
	}
}

int addr_equal(int *addr, int *dest, int *mask, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if ((addr[i] & mask[i]) != (dest[i] & mask[i]))
			return 0;

	return 1;
}

#define ipv4_to_addr(x,y) string_to_addr(x,y,IPV4_LEN,IPV4_WIDTH,IPV4_DELIM)
#define print_ipv4(x) print_addr(x,IPV4_LEN,IPV4_DELIM,IPV4_FORMAT)
#define ipv4_mask(x,y) addr_mask(x,y,IPV4_LEN,IPV4_BITS)
#define ipv4_equal(x,y,z) addr_equal(x,y,z,IPV4_LEN)

#define ipv6_to_addr(x,y) string_to_addr(x,y,IPV6_LEN,IPV6_WIDTH,IPV6_DELIM)
#define print_ipv6(x) print_addr(x,IPV6_LEN,IPV6_DELIM,IPV6_FORMAT)
#define ipv6_mask(x,y) addr_mask(x,y,IPV6_LEN,IPV6_BITS)
#define ipv6_equal(x,y,z) addr_equal(x,y,z,IPV6_LEN)

#define fc_to_addr(x,y) string_to_addr(x,y,FC_LEN,FC_WIDTH,FC_DELIM)
#define print_fc(x) print_addr(x,FC_LEN,FC_DELIM,FC_FORMAT)
#define fc_mask(x,y) addr_mask(x,y,FC_LEN,FC_BITS)
#define fc_equal(x,y,z) addr_equal(x,y,z,FC_LEN)

int main(int argc, char *argv[])
{
	int addr[IPV6_LEN];
	int addr2[IPV6_LEN];
	int mask[IPV6_LEN];
	int tmp[IPV6_LEN];
	char *ipv4 = "198.168.11.10";
	char *v4mask = "198.168.255.255";
	char *ipv4_2 = "198.168.11.10/16";
	char *ipv6 = "2001:db8:1234::/48";
	char *ipv6_2 = "2001:db8:85a3:0:0:8a2e:370:7334";
	char *fc = "10:00:08:00:5a:d0:97:9b/44";
	int n, x;

	n = ipv4_to_addr(ipv4, addr);
	print_ipv4(addr);
	if (n)
		printf("%d bits of mask\n", n);

	n = ipv4_to_addr(v4mask, mask);
	print_ipv4(mask);
	if (n) {
		printf("%d bits of mask\n", n);
		ipv4_mask(tmp, n);
		print_ipv4(tmp);
	}

	n = ipv4_to_addr(ipv4_2, addr2);
	print_ipv4(addr2);
	if (n) {
		printf("%d bits of mask\n", n);
		ipv4_mask(tmp, n);
		print_ipv4(tmp);
	}

	printf("addresses are on the %s subnet\n",
		ipv4_equal(addr, addr2, mask) ? "same" : "different");

	n = ipv6_to_addr(ipv6, addr);
	print_ipv6(addr);
	if (n) {
		printf("%d bits of mask\n", n);
		ipv6_mask(tmp, n);
		print_ipv6(tmp);
	}

	x = ipv6_to_addr(ipv6_2, addr);
	print_ipv6(addr);
	if (n) {
		printf("%d bits of mask\n", n);
		ipv6_mask(tmp, n);
		print_ipv6(tmp);
	}

	printf("addresses are on the %s subnet\n",
		ipv6_equal(addr, addr2, mask) ? "same" : "different");

	n = fc_to_addr(fc, addr);
	print_fc(addr);
	if (n) {
		printf("%d bits of mask\n", n);
		fc_mask(tmp, n);
		print_fc(tmp);
	}
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IPV4_ADDR_LEN 4
#define IPV6_ADDR_LEN 8
#define FC_ADDR_LEN 8
#define IPV4_BITS 8
#define IPV6_BITS 16
#define IPV4_WIDTH 3
#define IPV6_WIDTH 4

int ipv4_to_addr(char *p, int *addr)
{
	int mask[IPV4_ADDR_LEN];
	int bits;
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

void print_ipv4(int *addr)
{
	int i;

	for (i = 0; i < IPV4_ADDR_LEN; i++)
		printf("%s%d", i ? "." : "", addr[i]);
	printf("\n");
}

int ipv6_to_addr(char *p, int *addr)
{
	int mask[IPV6_ADDR_LEN];
	int bits;
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

void print_ipv6(int *addr)
{
	int i;

	for (i = 0; i < IPV6_ADDR_LEN; i++)
		printf("%s%x", i ? ":" : "", addr[i]);
	printf("\n");
}

void print_fc(int *addr)
{
	int i;

	for (i = 0; i < IPV6_ADDR_LEN; i++)
		printf("%s%02x", i ? ":" : "", addr[i]);
	printf("\n");
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

int main(int argc, char *argv[])
{
	int addr[IPV6_ADDR_LEN];
	int addr2[IPV6_ADDR_LEN];
	int mask[IPV6_ADDR_LEN];
	int tmp[IPV6_ADDR_LEN];
	char *ipv4 = "198.168.11.10";
	char *v4mask = "198.168.255.255";
	char *ipv4_2 = "198.168.11.10/16";
	char *ipv6 = "2001:db8:1234::/48";
	char *ipv6_2 = "2001:db8:85a3:0:0:8a2e:370:7334";
	char *fc = "10:00:08:00:5a:d0:97:9b";
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

	n = ipv6_to_addr(fc, addr);
	print_fc(addr);
	if (n)
		printf("%d bits of mask\n", n);
}

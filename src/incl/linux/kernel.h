/*
 * Definitions for the NVM Express interface
 * Copyright (c) 2011-2014, Intel Corporation.
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

#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

/* excerpt from kernel.h */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

/* excerpt from asm-generic/int-ll64.h */
typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

#ifdef __GNUC__
__extension__ typedef __signed__ long long __s64;
__extension__ typedef unsigned long long __u64;
#else
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif

#define u8	__u8
#define u16	__u16
#define u32	__u32
#define u64	__u64

/* complex for_each that checkpatch has issues with but is correct and
 * used in multiple include files in the kernel
 */
#define for_each_dir(entry, subdir)			\
	while ((entry = readdir(subdir)) != NULL)	\
		if (strcmp(entry->d_name, ".") &&	\
		    strcmp(entry->d_name, ".."))

#endif

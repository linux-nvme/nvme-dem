#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern int stopped;
enum { DISCONNECTED = 0, CONNECTED };

#define print_err(f, a...) printf("%s:%d " f "\n", __func__, __LINE__, ##a)

#define PAGE_SIZE 4096

#define u64 __u64
#define u32 __u32
#define u16 __u16
#define u8 __u8


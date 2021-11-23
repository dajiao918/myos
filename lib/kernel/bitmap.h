#ifndef _LIB_KERNEL_BITMAP_H
#define _LIB_KERNEL_BITMAP_H
#include "global.h"
#define BITMAP_USED 1
struct bitmap{
	uint32_t btmp_bytes_len;//位图的字节长度
	uint8_t* bits;//相当于一个字节数组
};

void bitmap_init(struct bitmap* btmp);
int bitmap_scan_test(struct bitmap* btmp, uint32_t bit_idx);
int bitmap_scan(struct bitmap* btmp, uint32_t cnt);
void bitmap_set(struct bitmap* btmp, uint32_t bit_idx, int8_t value);
#endif

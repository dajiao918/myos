#include "bitmap.h"
#include "debug.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"

/*初始化位图*/
void bitmap_init(struct bitmap* bmap) {
	memset(bmap->bits,0,bmap->btmp_bytes_len);
}

/*判断bit_idx位是否为1，为1返回true，否则返回false*/
int bitmap_scan_test(struct bitmap* btmp, uint32_t bit_idx) {
	//获取bit_idx位在bits数组中的字节位置
	uint32_t byte_idx = bit_idx / 8;
	//获取bit_idx在bits数组中第byte_idx字节的第几位
	uint32_t bit_odd = bit_idx % 8;

	return btmp->bits[byte_idx] & (BITMAP_USED << bit_odd);
}

/*在位图中连续申请cnt个位，成功返回首位，否则返回-1*/
int bitmap_scan(struct bitmap* btmp, uint32_t cnt) {
	uint32_t byte_idx = 0;
	//11111111族字节比较
	while(0xff == btmp->bits[byte_idx] && byte_idx < btmp->btmp_bytes_len) {
		byte_idx ++;
	}
	
	//找不到多与的内存空间，直接停止程序
	ASSERT(byte_idx < btmp->btmp_bytes_len);
	
	int32_t bit_idx = 0;
	//找到第一个为0的位
	while(btmp->bits[byte_idx] & (uint8_t) BITMAP_USED << bit_idx) {
		bit_idx ++;
	}

	uint32_t bit_idx_start = bit_idx + byte_idx*8;
	if(cnt == 1) {
		return bit_idx_start;
	}
	uint32_t next_bit = bit_idx_start + 1;
	//获取bit_idx右边的所有位长度
	uint32_t bits_right = btmp->btmp_bytes_len * 8 - bit_idx_start;
	bit_idx_start = -1;//先变为-1，观察情况
	uint32_t count = 1;//记录当前连续为0的bit
	
	while(bits_right -- > 0) {
		if(!bitmap_scan_test(btmp,next_bit)) {
			count ++;
		} else {
			count = 0;
		}
		if(count == cnt) {
			bit_idx_start = next_bit - cnt + 1;
			break;
		}
		next_bit ++;
	}
	return bit_idx_start;
}


//将bit_idx位的bit设位value
void bitmap_set(struct bitmap* btmp, uint32_t bit_idx, int8_t value) {
	
	ASSERT(value == 1 || value == 0);
	uint32_t byte_idx = bit_idx / 8;
        uint32_t bit_odd = bit_idx % 8;
	if(value) {
		btmp->bits[byte_idx] |= (BITMAP_USED << bit_odd);
	} else {
		btmp->bits[byte_idx] &= ~(BITMAP_USED << bit_odd);
	}
}


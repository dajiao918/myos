#ifndef _LIB_IO_H
#define _LIB_IO_H
#include "stdint.h"

//向端口中写入一个字节
static inline void outb(uint16_t port, uint8_t data) {
	
	//b代表al,w代表dx，汇编格式为：outb %al, %dx
	asm volatile("outb %b0,%w1"::"a"(data),"Nd"(port));
}

//向端口中写入ecx_num个数组
static inline void outsw(uint16_t port,const void* addr,uint32_t ecx_num) {
	
	asm volatile("cld; rep outsw":"+S"(addr), "+c"(ecx_num):"d"(port));
}

//读取端口的8位数据
static inline uint8_t inb(uint16_t port) {
	uint8_t data;
	asm volatile("inb %w1,%b0":"=a"(data) : "Nd"(port));
	return data;
}

//读取端口的ecx_num个数据
static inline void insw(uint16_t port,const void* addr,uint32_t ecx_num) {
	asm volatile("cld; rep insw":"+D"(addr),"+c"(ecx_num): "d"(port):"memory");
}
#endif

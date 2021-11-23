#include "stdio.h"
#include "stdint.h"
#include "global.h"
#include "stdio-kernel.h"
#include "console.h"

/* 格式化输出字符串format */
void printk(const char* format, ...) {
   va_list args;
   va_start(args, format);	       // 使args指向format
   char buf[1024] = {0};	       // 用于存储拼接后的字符串
   vsprintf(buf, format, args);
   va_end(args);
   console_put_str(buf); 
}
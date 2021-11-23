#include "debug.h"
#include "print.h"
#include "interrupt.h"

void panic_spin(char* filename, int line, const char* func,const char* condition) {
	intr_disable();//关闭中断，出现错误了，需要打印错误信息
	put_str("\n\n\nHERO GUANGGE,error occur!!!\n");
	put_str("filename: ");put_str(filename);
	put_str("\n");
	put_str("line: 0x");put_int(line);
	put_str("\n");
	put_str("function: ");put_str((char*) func);
	put_str("\n");
	put_str("condition: ");put_str((char*) condition);
	put_str("\n");
	while(1);

}

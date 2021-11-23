#ifndef _KERNEL_DEBUG_H
#define _KERNEL_DEBUG_H

//声明程序错误函数
void panic_spin(char* filename, int line, const char* func, const char* condition);

/*...表示可变的参数个数，_FILE_ - _func_都是预定义宏，_VA_ARGS_则表示...*/
#define PANIC(...) panic_spin(__FILE__,__LINE__,__func__,__VA_ARGS__)

#ifdef NDEBUG
	#define ASSERT(CONDITION) ((void)0)
#else /*#号表示将CONDITION转换为字符串，例如var!=0，转换为"var!=0"*/
	#define ASSERT(CONDITION) if(CONDITION) {} else { PANIC(#CONDITION); }
#endif /*end NDEBUG*/

#endif

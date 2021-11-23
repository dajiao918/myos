#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "ioqueue.h"
#include "keyboard.h"
#include "process.h"
#include "syscall.h"
#include "syscall-init.h"
#include "debug.h"
#include "stdio.h"
#include "fs.h"
#include "string.h"
#include "stdio-kernel.h"
#include "dir.h"
#include "shell.h"

void init(void);
void test(void);

extern void cls_screen(void);

int main(void) {
	put_str("I am a kernel\n");
	init_all();
	/************* 写入应用程序 *************/
	// uint32_t file_size = 14080;
	// uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
	// struct disk* sda = &channels[0].devices[0];
	// void* prog_buf = sys_malloc(file_size);
	// ide_read(sda, 300, prog_buf, sec_cnt);
	// int32_t fd = sys_open("/prog_no_arg", O_CREAT|O_RDWR);
	// if (fd != -1) {
	// 	if(sys_write(fd, prog_buf, file_size) == -1) {
	// 		printk("file write, error!\n");
	// 		while(1);
	// 	}
	// }
	/************* 写入应用程序结束 *************/
	put_str("I love you,Mrs chen!\n");
	printf("[Mr's chen@localhost /]$");
	intr_enable();	
	// process_execute(test,"test");
	while(1);
	return 0;
}

void init(void) {
	uint32_t ret_pid = fork();
	if(ret_pid) {
		while(1);
	} else {
		myshell();
	}
	PANIC("init: should not be here\n");
}

void test(void) {
	uint32_t ret_pid = fork();
	if(ret_pid) {
		printf("i am father, my pid is %d, child pid is %d\n",getpid(),ret_pid);
	} else {
		printf("i am child my pid is %d,ret_pid is %d\n",getpid(),ret_pid);
	}
	while(1);
}
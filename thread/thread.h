#ifndef THREAD_THREAD_H
#define THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "interrupt.h"
#include "memory.h"

typedef void thread_func(void*);
#define TASK_NAME_LEN 16
#define PG_SIZE 4096
#define MAX_FILE_OPEN_PER_PROC 8
enum task_status{
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_WAITING,
	TASK_HANGING,
	TASK_DIED
};

//中断栈
struct intr_stack{
	uint32_t vec_no;//kernel.s发出的终端号
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp_dummy;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;
	
	uint32_t err_code;
	void (*eip) (void);
	uint32_t cs;
	uint32_t eflags;
	void *esp;
	uint32_t ss;
};

struct thread_stack{
	uint32_t ebp;
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;
	
	void (*eip) (thread_func* func, void* func_arg);
	void (*unused_retaddr);
	thread_func* func;
	void* func_arg;
};

struct task_struct{
	uint32_t* self_kstack;
	uint16_t pid;
	enum task_status status;
	uint8_t priority;
	char name[16];

	uint8_t ticks;
	uint32_t elapsed_ticks;

	int32_t fd_table[MAX_FILE_OPEN_PER_PROC];
	//线程在ready中的节点
	struct list_elem general_tag;
	struct list_elem all_list_tag;

	uint32_t* pgdir;
	struct virtual_addr userprog_vaddr;//用户进程的虚拟地址
	struct mem_block_desc u_block_desc[DESC_CNT];
	uint32_t cwd_inode_nr;//进程当前所在的工作目录
	int32_t parent_pid;//父进程的pid
	uint32_t stack_magic;
};

extern struct list thread_ready_list;
extern struct list thread_all_list;

struct task_struct* thread_start(char* name,int prio, thread_func funtion, void* func_arg);
void init_thread(struct task_struct* pthread,char* name, int prio);
void thread_create(struct task_struct* pthread,thread_func func,void* func_arg);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct* pthread);
void schedule(void);
struct task_struct* running_thread(void);
void thread_yield(void);
uint16_t fork_pid(void);
void sys_ps(void);
#endif

#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "memory.h"
#include "global.h"
#include "list.h"
#include "interrupt.h"
#include "debug.h"
#include "print.h"
#include "process.h"
#include "sync.h"
#include "stdio-kernel.h"
#include "fs.h"
#include "file.h"
#include "stdio.h"

struct task_struct* main_thread;
struct list thread_all_list;
struct list thread_ready_list;
struct list_elem* thread_tag;
struct lock pid_lock;
struct task_struct* idle_thread;

extern void switch_to(struct task_struct* cur,struct task_struct* next);
extern void init(void);

static void idle(void* arg UNUSED) {
	while(1) {
		thread_block(TASK_BLOCKED);
		asm volatile("sti;hlt": : :"memory");
	}
}

static uint16_t allocate_pid(void) {
	static uint16_t next_pid = 0;
        lock_acquire(&pid_lock);
	next_pid ++;
	lock_release(&pid_lock);
	return next_pid;
}

uint16_t fork_pid(void){
	return allocate_pid();
}

struct task_struct* running_thread(){
	uint32_t esp;
	asm ("mov %%esp,%0":"=g"(esp));
	return (struct task_struct*) (esp & 0xfffff000);
}

void kernel_thread(thread_func* function,void* func_arg) {
	intr_enable();
	function(func_arg);
}

void thread_create(struct task_struct* pthread,thread_func func,void* func_arg){
	pthread->self_kstack -= sizeof(struct intr_stack);
	pthread->self_kstack -= sizeof(struct thread_stack);
	
	struct thread_stack* kthread_stack = (struct thread_stack*) pthread->self_kstack;
	kthread_stack->eip = kernel_thread;
	kthread_stack->func = func;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

void init_thread(struct task_struct* pthread,char* name, int prio) {

	memset(pthread,0,sizeof(struct task_struct));
	strcpy(pthread->name,name);

	if(pthread == main_thread){
		pthread->status = TASK_RUNNING;
	} else {
		pthread->status = TASK_READY;
	}

	pthread->priority = prio;
	pthread->pid = allocate_pid();
	pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
	pthread->ticks = prio;
	pthread->elapsed_ticks = 0;
	pthread->pgdir = NULL;
	pthread->parent_pid = -1;

	pthread->fd_table[0] = 0;
	pthread->fd_table[1] = 1;
	pthread->fd_table[2] = 2;
	uint8_t fd_idx = 3;
	while(fd_idx < MAX_FILE_OPEN_PER_PROC) {
		pthread->fd_table[fd_idx] = -1;
		fd_idx++;
	}
	pthread->cwd_inode_nr = 0;//根目录作为默认工作目录
	pthread->stack_magic = 0x12345678;
}

struct task_struct* thread_start(char* name,int prio, thread_func function, void* func_arg){
	//使pcb指向申请页表的虚拟起始地址
	struct task_struct* thread = get_kernel_pages(1);
	init_thread(thread,name,prio);
	thread_create(thread,function,func_arg);
	
	ASSERT(!elem_find(&thread_ready_list,&thread->general_tag));
	list_append(&thread_ready_list,&thread->general_tag);
	ASSERT(!elem_find(&thread_all_list,&thread->all_list_tag));
	list_append(&thread_all_list,&thread->all_list_tag);
	return thread;
}

static void make_main_thread(void) {
	main_thread = running_thread();
	init_thread(main_thread,"main",31);
	ASSERT(!elem_find(&thread_all_list,&main_thread->all_list_tag));
	list_append(&thread_all_list,&main_thread->all_list_tag);
}

void schedule(){
	ASSERT(intr_get_status() == INTR_OFF);
	struct task_struct* cur = running_thread();
	if(cur->status == TASK_RUNNING) {
		ASSERT(!elem_find(&thread_ready_list,&cur->general_tag));
		list_append(&thread_ready_list,&cur->general_tag);
		cur->ticks = cur->priority;
		cur->status = TASK_READY;
	} else {}
	if(list_empty(&thread_ready_list)){
		thread_unblock(idle_thread);
	}
	thread_tag = list_pop(&thread_ready_list);
	struct task_struct* next = elem2entry(struct task_struct,general_tag,thread_tag);
	// put_str("cur_thread: ");put_str(cur->name);
	// put_char('\n');
	// put_str("next_thread: ");put_str(next->name);
	// put_char('\n');
	next->status = TASK_RUNNING;
	process_activate(next);
	switch_to(cur,next);
}

void thread_block(enum task_status stat) {
	ASSERT((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING));
	enum intr_status old_status = intr_disable();
	struct task_struct* cur = running_thread();
	cur->status = stat;
	schedule();
	intr_set_status(old_status);
}

void thread_unblock(struct task_struct* pthread) {
	enum intr_status old_status = intr_disable();
	ASSERT((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING));
	if(pthread->status != TASK_READY) {
		ASSERT(!elem_find(&thread_ready_list,&pthread->general_tag));
		if(elem_find(&thread_ready_list,&pthread->general_tag)) {
			PANIC("thread_unblock: blocked thread in ready list");
		}
		list_push(&thread_ready_list,&pthread->general_tag);
		pthread->status = TASK_READY;
	}
	intr_set_status(old_status);
}

void thread_yield(void) {
	struct task_struct* cur = running_thread();
	enum intr_status old_status = intr_disable();
	ASSERT(!elem_find(&thread_ready_list,&cur->general_tag));
	list_append(&thread_ready_list,&cur->general_tag);
	cur->status = TASK_READY;
	schedule();
	intr_set_status(old_status);
}

void thread_init(void){
	put_str("thread_init start\n");
	list_init(&thread_all_list);
	list_init(&thread_ready_list);
	lock_init(&pid_lock);
	process_execute(init,"init");
	make_main_thread();
	idle_thread = thread_start("idle",10,idle,NULL);
	put_str("thread_init done\n");
}

static void pad_print(char* buf, int32_t buf_len, void* ptr,char format){
	memset(buf,0,buf_len);
	uint8_t out_pad_idx = 0;
	switch(format) {
		case 's':
			out_pad_idx = sprintf(buf,"%s",ptr);
			break;
		case 'd':
			out_pad_idx = sprintf(buf,"%d",*((int16_t*)ptr));
			break;
		case 'x':
			out_pad_idx = sprintf(buf,"%d",*((int32_t*)ptr));
			break;
	}
	while(out_pad_idx < buf_len) {
		buf[out_pad_idx] = ' ';
		out_pad_idx ++;
	}
	sys_write(stdout_no,buf,buf_len-1);
} 


static bool elem2thread_info(struct list_elem* pelem, int arg UNUSED) {
	struct task_struct* pthread = elem2entry(struct task_struct,all_list_tag,pelem);
	char out_pad[16] = {0};
	pad_print(out_pad,16,&pthread->pid,'d');
	if(pthread->parent_pid == -1) {
		pad_print(out_pad,16,"NULL",'s');
	} else {
		pad_print(out_pad,16,&pthread->parent_pid,'d');
	}

	switch(pthread->status) {
		case 0:
			pad_print(out_pad,16,"RUNNING",'s');
			break;
		case 1:
			pad_print(out_pad,16,"READY",'s');
			break;
		case 2:
			pad_print(out_pad,16,"BLOCKED",'s');
			break;
		case 3:
			pad_print(out_pad,16,"WAITING",'s');
			break;
		case 4:
			pad_print(out_pad,16,"HANGING",'s');
			break;
		case 5:
			pad_print(out_pad,16,"DIED",'s');
			break;
	}
	pad_print(out_pad,16,&pthread->elapsed_ticks,'x');
	memset(out_pad, 0, 16);
	ASSERT(strlen(pthread->name) < 17);
	memcpy(out_pad,pthread->name,strlen(pthread->name));
	strcat(out_pad,"\n");
	sys_write(stdout_no,out_pad,strlen(out_pad));
	return false;
}


void sys_ps(void) {
	char* ps_title = "PID             PPID            STAT\
	            TICKS           COMMAND\n";
	sys_write(stdout_no,ps_title,strlen(ps_title));
	list_traversal(&thread_all_list,elem2thread_info,0);	
}



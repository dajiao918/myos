#ifndef _USERPROG_PROCESS_H
#define _USERPROG_PROCESS_H

#define USER_ADDR_START 0x8048000
#define USER_STACK3_VADDR (0xc0000000-0x1000)
#define default_prio 30

void process_activate(struct task_struct* thread);
void start_process(void* _filename);
void process_execute(void* _filename,char* name);
void page_dir_activate(struct task_struct* pthread);
uint32_t* create_page_dir(void);
#endif

#include "stdint.h"
#include "global.h"
#include "thread.h"
#include "memory.h"
#include "process.h"
#include "debug.h"
#include "string.h"
#include "bitmap.h"
#include "file.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"

extern void intr_exit(void);

/*复制父进程的内核栈以及虚拟位图*/
static int32_t copy_pcb_vaddrbitmap_stack0(\
struct task_struct* child_thread, struct task_struct* parent_thread){
    /*copy entern core stack*/
    memcpy(child_thread,parent_thread,PG_SIZE);
    child_thread->pid = fork_pid();
    child_thread->elapsed_ticks = 0;
    child_thread->status = TASK_READY;
    child_thread->ticks = child_thread->priority;
    child_thread->parent_pid = parent_thread->pid;
    child_thread->general_tag.next = child_thread->general_tag.prev = NULL;
    child_thread->all_list_tag.next = child_thread->all_list_tag.prev = NULL;
    block_desc_init(child_thread->u_block_desc);
    /*进程虚拟地址池的位图物理页数量*/
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP(\
    (0xc0000000 - USER_ADDR_START) / PG_SIZE / 8,PG_SIZE);
    void* vaddr_btmp = get_kernel_pages(bitmap_pg_cnt);
    //copy parent thread virtual bitmap to child thread 
    memcpy(vaddr_btmp,child_thread->userprog_vaddr.vaddr_bitmap.bits,
    bitmap_pg_cnt * PG_SIZE);
    //childthread bits points attach vaddr_btmp
    child_thread->userprog_vaddr.vaddr_bitmap.bits = vaddr_btmp;
    ASSERT(strlen(child_thread->name) < 11);
    strcat(child_thread->name,"_fork");
    return 0;   
}

/*将父进程的所有内存数据复制到子进程中*/
static void copy_body_stack3(struct task_struct* child_thread, struct task_struct* parent_thread,
void* buf_page) {
    uint8_t* vaddr_btmp = parent_thread->userprog_vaddr.vaddr_bitmap.bits;
    uint32_t bimp_bytes_len = parent_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;
    uint32_t vaddr_start = parent_thread->userprog_vaddr.vaddr_start;   
    uint32_t idx_byte = 0;
    uint32_t idx_bit = 0;
    uint32_t prog_vaddr = 0;
    while(idx_byte < bimp_bytes_len) {
        if(vaddr_btmp[idx_byte]) {
            idx_bit = 0;
            while(idx_bit < 8) {
                if((BITMAP_USED << idx_bit) & vaddr_btmp[idx_byte]) {
                    prog_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;
                    //将父进程的物理也数据复制到缓冲区中
                    memcpy(buf_page,(void*)prog_vaddr,PG_SIZE);
                    //切换到子进程cr3
                    page_dir_activate(child_thread);
                    //为子进程的prog_vaddr申请物理页
                    get_a_page_without_opvaddrbitmap(PF_USER,prog_vaddr);
                    //将buf_page复制到子进程中
                    memcpy((void*)prog_vaddr,buf_page,PG_SIZE);
                    //切换回父进程的cr3
                    page_dir_activate(parent_thread);
                }
                idx_bit ++;
            }
        }
        idx_byte ++;
    }
}

/*为子进程创建一个栈，在switch_to函数中返回时，直接返回到intr_exit
  然后将内核栈中的数据pop到寄存器中，这样就能顺着fork之后的代码执行*/
static int32_t build_child_stack(struct task_struct* child_thread){
    struct intr_stack* intr_0_stack = (struct intr_stack*) \
    ((uint32_t)child_thread + PG_SIZE - sizeof(struct intr_stack));
    //从内核栈退出后，eax的值即为fork函数的返回值，子进程的返回的是0
    intr_0_stack->eax = 0;
    //在intr_0_stack之下创建一个栈
    uint32_t* ret_addr_in_thread_stack = (uint32_t*)intr_0_stack - 1;
    uint32_t* esi_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 2;
    uint32_t* edi_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 3;
    uint32_t* ebx_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 4;
    uint32_t* ebp_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 5;
    //switch_to 函数执行pop ebp,ebx,edi,esi,之后ret此时ip的值就等于ret_addr_in_thread_Stack
    *ret_addr_in_thread_stack = intr_exit;
    //switch_to函数获取到esp就是此值
    child_thread->self_kstack = ebp_ptr_in_thread_stack;
    return 0;
}

/*修改inode的打开次数*/
static void update_inode_open_cnts(struct task_struct* thread) {
    int32_t local_fd = 3, global_fd = 0;
    while(local_fd < MAX_FILE_OPEN_PER_PROC) {
        global_fd = thread->fd_table[local_fd];
        if(global_fd != -1) {
            file_table[global_fd].fd_inode->i_open_cnts ++;
        }
        local_fd ++;
    }
}

static int32_t copy_process(struct task_struct* child_thread,
struct task_struct* parent_thread) {
    void* buf_page = get_kernel_pages(1);
    if(buf_page == NULL) {
        return -1;
    }

    if(copy_pcb_vaddrbitmap_stack0(child_thread,parent_thread) == -1){
        return -1;
    }

    child_thread->pgdir = create_page_dir();
    if(child_thread->pgdir == NULL) {
        return -1;
    }

    copy_body_stack3(child_thread,parent_thread,buf_page);
    build_child_stack(child_thread);
    update_inode_open_cnts(child_thread);
    mfree_page(PF_KERNEL,buf_page,1);
    return 0;
}

uint16_t sys_fork(void){
    struct task_struct* parent_thread = running_thread();
    struct task_struct* child_thread = get_kernel_pages(1);
    if(child_thread == NULL) {
        return -1;
    }

    ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);
    if(copy_process(child_thread,parent_thread) == -1) {
        return -1;
    }
    ASSERT(!elem_find(&thread_ready_list,&child_thread->general_tag));
    list_append(&thread_ready_list,&child_thread->general_tag);
    ASSERT(!elem_find(&thread_all_list,&child_thread->all_list_tag));
    list_append(&thread_all_list,&child_thread->all_list_tag);
    return child_thread->pid;
}

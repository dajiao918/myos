#include "inode.h"
#include "stdint.h"
#include "global.h"
#include "debug.h"
#include "ide.h"
#include "string.h"
#include "list.h"
#include "thread.h"
#include "memory.h"
#include "interrupt.h"
#include "file.h"
#include "stdio-kernel.h"
#include "super_block.h"

struct inode_position{
    bool two_sec;//if inode place two sector
    uint32_t sec_lba;//the sector number of inode place
    uint32_t off_size;//inode在扇区内的字节偏移量
};

//获取inode所在的扇区和inode在扇区内的偏移量
static void inode_locate(struct partition* part,uint32_t inode_no,
struct inode_position* inode_pos) {

    ASSERT(inode_no < 4096);
    uint32_t inode_table_lba = part->sb->inode_table_lba;

    uint32_t inode_size = sizeof(struct inode);

    //inode想对于inode_table_lba的字节偏移量
    uint32_t off_size = inode_no * inode_size;
    //inode想对于inode_table_lba的扇区偏移量
    uint32_t off_sec = off_size / 512;
    //inode想对于所在扇区的偏移量
    uint32_t off_size_in_sec = off_size % 512;

    //inode所在扇区在从inode开始计算之后的容量
    uint32_t right_in_sec = 512-off_size_in_sec;
    if(right_in_sec >= inode_size) {
        inode_pos->two_sec = false;
    } else {
        inode_pos->two_sec = true;
    }
    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

void inode_sync(struct partition* part, struct inode* inode, void* io_buf) {

    uint8_t inode_no = inode->i_no;
    struct inode_position inode_pos;
    inode_locate(part,inode_no,&inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba+part->sec_cnt));

    struct inode pure_inode;

    memcpy(&pure_inode,inode,sizeof(struct inode));
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;

    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    char* inode_buf = (char*) io_buf;
    if(inode_pos.two_sec) {
        //如果跨越两个扇区，先读当前inode的所在两个扇区
        ide_read(part->my_disk,inode_pos.sec_lba,inode_buf,2);
        //然后将pure_inode copy到inode_buf中，偏移就是inode在其所在扇区中的偏移
        memcpy((inode_buf+inode_pos.off_size),&pure_inode,sizeof(struct inode));
        ide_write(part->my_disk,inode_pos.sec_lba,inode_buf,2);
    } else {
        ide_read(part->my_disk,inode_pos.sec_lba,inode_buf,1);
        memcpy((inode_buf+inode_pos.off_size),&pure_inode,sizeof(struct inode));
        ide_write(part->my_disk,inode_pos.sec_lba,inode_buf,1);

    }
}

struct inode* inode_open(struct partition* part,uint32_t inode_no) {

    //先在打开的inode链表中寻找inode
    struct list_elem* elem = part->open_inodes.head.next;
    struct inode* inode_found;
    while(elem != &part->open_inodes.tail) {
        inode_found = elem2entry(struct inode,inode_tag,elem);
        if(inode_found->i_no == inode_no) {
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    struct inode_position inode_pos;
    inode_locate(part,inode_no,&inode_pos);
    //为了使被创建的inode被所有的进程共享，需要在内核空间创建inode 
    struct task_struct* cur = running_thread();
    uint32_t* cur_pgdir = cur->pgdir;
    cur->pgdir = NULL;
    inode_found = (struct inode*)sys_malloc(sizeof(struct inode));
    cur->pgdir = cur_pgdir;
    char* inode_buf;
    if(inode_pos.two_sec) {
        inode_buf = (char*) sys_malloc(1024);
        ide_read(part->my_disk,inode_pos.sec_lba,inode_buf,2);

    } else {
        inode_buf = (char*) sys_malloc(512);
        ide_read(part->my_disk,inode_pos.sec_lba,inode_buf,1);
    }
    memcpy(inode_found,(inode_buf+inode_pos.off_size),sizeof(struct inode));
    list_push(&part->open_inodes,&inode_found->inode_tag);
    inode_found->i_open_cnts = 1;
    sys_free(inode_buf);
    return inode_found;
}

void inode_close(struct inode* inode) {
    enum intr_status old_status = intr_disable();
    if(--inode->i_open_cnts == 0) {
        list_remove(&inode->inode_tag);

        //释放的inode所占据的内存也是内核内存，需要先将进程的pgdir变null
        struct task_struct* cur = running_thread();
        uint32_t* cur_pgdir = cur->pgdir;
        cur->pgdir = NULL;
        sys_free(inode);
        cur->pgdir = cur_pgdir;
    }
    intr_set_status(old_status);
}

void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf) {
    ASSERT(inode_no < 4096);
    // printk("inode_table delete begin\n");
    struct inode_position inode_pos;

    inode_locate(part,inode_no,&inode_pos);

    char* inode_buf = (char*) io_buf;
    if(inode_pos.two_sec) {
        ide_read(part->my_disk,inode_pos.sec_lba,inode_buf,2);
        memset(inode_buf+inode_pos.off_size,0,sizeof(struct inode));
        ide_write(part->my_disk,inode_pos.sec_lba,io_buf,2);
    } else {
        ide_read(part->my_disk,inode_pos.sec_lba,inode_buf,1);
        memset(inode_buf+inode_pos.off_size,0,sizeof(struct inode));
        ide_write(part->my_disk,inode_pos.sec_lba,io_buf,1);
    }
    // printk("inode_table delete end\n");
}

/*回收iinode指向的数据块，以及回收该inode所占的位图bit*/
void inode_release(struct partition* part, uint32_t inode_no) {

    // printk("inode release() start\n");

    struct inode* inode_to_del = inode_open(part,inode_no);
    ASSERT(inode_to_del->i_no == inode_no);

    uint8_t block_idx = 0, block_cnt = 12;
    uint32_t block_bitmap_idx;
    uint32_t all_blocks[140] = {0};

    while(block_idx < 12) {
        all_blocks[block_idx] = inode_to_del->i_sectors[block_idx];
        block_idx++;
    }
    /*如果间接块不为null，那么先读取间接块下的所有块到all_blocks
     *然后回收间接块*/
    if(inode_to_del->i_sectors[12] != 0) {
        ide_read(part->my_disk,inode_to_del->i_sectors[12],
        all_blocks+12,1);
        block_cnt = 140;
        block_bitmap_idx = inode_to_del->i_sectors[12]-part->sb->data_start_lba;
        ASSERT(block_bitmap_idx > 0);
        bitmap_set(&part->block_bitmap,block_bitmap_idx,0);
        bitmap_sync(part,block_bitmap_idx,BLOCK_BITMAP);
    }

    block_idx = 0;
    while(block_idx < block_cnt) {
        if(all_blocks[block_idx] != 0) {
            block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            ASSERT(block_bitmap_idx > 0);
            bitmap_set(&part->block_bitmap,block_bitmap_idx,0);
            bitmap_sync(part,block_bitmap_idx,BLOCK_BITMAP);
        }
        block_idx ++;
    }
    bitmap_set(&part->inode_bitmap,inode_no,0);
    bitmap_sync(part,inode_no,INODE_BITMAP);
    
    /*清除inode_table*/
    void* io_buf = sys_malloc(1024);
    inode_delete(part,inode_no,io_buf);
    sys_free(io_buf);
    // printk("inode release() ok\n");

    inode_close(inode_to_del);
}

//初始化inode
void inode_init(uint32_t inode_no, struct inode* new_inode) {
    new_inode->i_no = inode_no;
    new_inode->i_open_cnts = 0;
    new_inode->i_size = 0;
    new_inode->write_deny = false;
    uint8_t sec_idx = 0;
    while(sec_idx < 13) {
        new_inode->i_sectors[sec_idx] = 0;
        sec_idx ++;
    }
}

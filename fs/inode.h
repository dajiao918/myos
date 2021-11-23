#ifndef _FS_INODE_H
#define _FS_INODE_H

#include "stdint.h"
#include "global.h"
#include "list.h"
#include "ide.h"

struct inode
{
    //the id of inode
    uint32_t i_no;
    /*if inode respent file, i_size is
    the size of file, otherwise is all of 
    directory size*/
    uint32_t i_size;
    //the file or directory haven been opened
    uint32_t i_open_cnts;
    //permit wirte, prevent multiply process
    bool write_deny;
    
    uint32_t i_sectors[13];
    struct list_elem inode_tag;
};

void inode_sync(struct partition* part, struct inode* inode, void* io_buf);
struct inode* inode_open(struct partition* part,uint32_t inode_no);
void inode_close(struct inode* inode);
void inode_init(uint32_t inode_no, struct inode* new_inode);
void inode_release(struct partition* part, uint32_t inode_no);
#endif
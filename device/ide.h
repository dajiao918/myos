#ifndef _DEVICE_IDE_H
#define _DEVICE_IDE_H

#include "stdint.h"
#include "global.h"
#include "bitmap.h"
#include "list.h"
#include "sync.h"
#include "super_block.h"

struct partition{
    uint32_t start_lba; //起始扇区
    uint32_t sec_cnt; //扇区数量
    struct disk* my_disk;//belog to which disk
    struct list_elem part_tag;//the tag int partition queue
    char name[8];//patiotion name
    struct super_block* sb;
    struct bitmap block_bitmap;
    struct bitmap inode_bitmap;
    struct list open_inodes;
};

struct disk
{
    char name[8]; //disk name
    struct ide_channel* my_channel;//belong to which ide_channle
    uint8_t dev_no;//is master disk 0,or slave disk 1
    struct partition prim_parts[4];//suppose four master partition
    struct partition logic_parts[8];//suppose eight logic partition
};

struct ide_channel
{
    char name[8];//ata channel name
    uint16_t port_base;//channel base port
    uint8_t irq_no;//channel interrupt number
    struct lock lock;
    bool expecting_intr;//wait disk occur interrupt
    struct semaphore disk_done;//block or notify progroma
    struct disk devices[2];//per channel has two disks, one master,one slave
};

extern uint8_t channel_cnt;
extern struct ide_channel channels[2];
extern struct list partition_list;
void ide_init(void);
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
#endif
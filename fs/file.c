#include "stdint.h"
#include "global.h"
#include "memory.h"
#include "stdio-kernel.h"
#include "file.h"
#include "thread.h"
#include "bitmap.h"
#include "ide.h"
#include "list.h"
#include "debug.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"


struct file file_table[MAX_FILE_OPEN];

int32_t get_free_slot_in_global(void) {
    
    uint32_t fd_idx = 3;//跨过标准输入，输出，错误
    while(fd_idx < MAX_FILE_OPEN) {
        if(file_table[fd_idx].fd_inode == NULL) {
            break;
        }
        fd_idx ++;
    }
    if(fd_idx == MAX_FILE_OPEN) {
        printk("exceeded max open files\n");
        return -1;
    }
    return fd_idx;
}

int32_t pcb_fd_install(int32_t global_fd_idx) {
    struct task_struct* cur = running_thread();
    uint8_t local_fd_idx = 0;
    while(local_fd_idx < MAX_FILE_OPEN_PER_PROC) {
        if(cur->fd_table[local_fd_idx] == -1) {
            cur->fd_table[local_fd_idx] = global_fd_idx;
            break;
        }
        local_fd_idx ++;
    }
    if(local_fd_idx == MAX_FILE_OPEN_PER_PROC) {
        printk("exceeded max open files_per_prog");
        return -1;
    }
    return local_fd_idx;
}   

int32_t inode_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap,1);
    if(bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap,bit_idx,1);
    return bit_idx;
}
//块位图申请，返回块的扇区号part->sb->data_start_lba+bit_idx
int32_t block_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->block_bitmap,1);
    if(bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->block_bitmap,bit_idx,1);
    return (part->sb->data_start_lba+bit_idx);
}
//将bit_idx位的位图同步到硬盘中，btmp表示块位图或者inode位图
void bitmap_sync(struct partition* part, uint32_t bit_idx,uint8_t btmp) {
    //本i节点索引相对于位图的扇区偏移量
    uint32_t off_sec = bit_idx / 4096;
    //相对于位图的字节偏移量
    uint32_t off_size = bit_idx / 8;

    uint32_t sec_lba;
    uint8_t* bitmap_off;
    switch (btmp)
    {
        case INODE_BITMAP:
            sec_lba = part->sb->inode_bitmap_lba + off_sec;
            bitmap_off = part->inode_bitmap.bits + off_size;
            break;
        case BLOCK_BITMAP:
            sec_lba = part->sb->block_bitmap_lba + off_sec;
            bitmap_off = part->block_bitmap.bits + off_size;
            break;
    }
    ide_write(part->my_disk,sec_lba,bitmap_off,1);
}

/*在parent_dir目录下创建一个文件，返回文件描述符*/
int32_t file_create(struct dir* parent_dir,char* filename, uint8_t flag) {

    //public io memory 
    void* io_buf = sys_malloc(1024);
    if(io_buf == NULL) {
        printk("in file_create: sys_malloc for io_buf failed!\n");
        return -1;
    }

    uint8_t rollback_step = 0;

    int32_t inode_no = inode_bitmap_alloc(cur_part);

    if(inode_no == -1) {
        printk("in file create: allocate inode failed!\n");
        return -1;
    }

    struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode));
    if(new_file_inode == NULL) {
        printk("file create: sys_malloc for inode failed!\n");
        rollback_step = 1;
        goto rollback;
    }

    inode_init(inode_no,new_file_inode);

    int fd_idx = get_free_slot_in_global();

    if(fd_idx == -1) {
        printk("file create: exceed max open files!\n");
        rollback_step = 2;
        goto rollback;
    }

    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_inode->write_deny = false;

    struct dir_entry new_dir_entry;
    memset(&new_dir_entry,0,sizeof(struct dir_entry));

    create_dir_entry(filename,inode_no,FT_REGULAR,&new_dir_entry);
    if(!sync_dir_entry(parent_dir,&new_dir_entry,io_buf)) {
        printk("file create :sync dir_entry failed!\n");
        rollback_step = 3;
        goto rollback;
    }

    memset(io_buf,0,1024);
    //同步的时候可能已经改变了inode->i_sectors，需要同步
    //以及inode->i_size
    inode_sync(cur_part,parent_dir->inode,io_buf);
    memset(io_buf,0,1024);
    //新文件的inode也需要被同步到相应的扇区上
    inode_sync(cur_part,new_file_inode,io_buf);
    //硬盘上的inode位图也需要被同步
    bitmap_sync(cur_part,inode_no,INODE_BITMAP);

    list_push(&cur_part->open_inodes,&new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;
    sys_free(io_buf);
    return pcb_fd_install(fd_idx);

rollback:
    switch (rollback_step)
    {
        case 3:
            memset(&file_table[fd_idx],0,sizeof(struct file));
            break;
        case 2:
            sys_free(new_file_inode);
            break;
        case 1:
            bitmap_set(&cur_part->inode_bitmap,inode_no,0);
            break;
    }
    sys_free(io_buf);
    return -1;
}

int32_t file_open(uint32_t inode_no, uint8_t flag) {
    int fd_idx = get_free_slot_in_global();
    if(fd_idx == -1) {
        printk("exceeded max open files\n");
        return -1;
    }
    //打开inode
    file_table[fd_idx].fd_inode = inode_open(cur_part,inode_no);
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;

    bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;
    if(flag & O_WRONLY || flag & O_RDWR) {
        //只要是可写，就需要判断是否有其他进程写次文件
        enum intr_status old_status = intr_disable();
        if(!(*write_deny)) {
            //if no other process write the file
            *write_deny = true;
            intr_set_status(old_status);
        } else {
            intr_set_status(old_status);
            printk("file can't be write now, try again later\n");
            return -1;
        }
    }
    return pcb_fd_install(fd_idx);

}

int32_t file_close(struct file* file) {
    if(file == NULL) {
        return -1;
    }
    file->fd_inode->write_deny = false;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;
    return 0;
}

/*把buf中的count个字节写入file*/
int32_t file_write(struct file* file, const void* buf, uint32_t count) {
    
    
    if((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140)) {
        printk("exceed max file_size 71680 bytes, write file failed\n");
        return -1;
    }
    //写入的缓冲区
    uint8_t* io_buf = sys_malloc(512);
    if(io_buf == NULL) {
        printk("file_write: sys_malloc for io_buf failed\n");
        return -1;
    }
    uint32_t* all_blocks = (uint32_t*) sys_malloc(BLOCK_SIZE+48);

    if(all_blocks == NULL) {
        printk("file_write: sys_malloc for all_blocks failed\n");
        return -1;
    }

    const uint8_t* src = buf;
    int32_t block_lba = -1; //块的扇区
    uint32_t block_bitmap_idx = 0;//块的下标
    int32_t block_idx;//数据块下标
    uint32_t indirect_block_table ;
    if(file->fd_inode->i_sectors[0] == 0) {
        block_lba = block_bitmap_alloc(cur_part);
        if(block_lba == -1) {
            printk("file_write: block_bitmap_alloc failed!\n");
            return -1;
        }
        file->fd_inode->i_sectors[0] = block_lba;
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        ASSERT(block_bitmap_idx != 0);
        bitmap_sync(cur_part,block_bitmap_idx,BLOCK_BITMAP);
    }

    uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;

   /* 存储count字节后该文件将占用的块数 */
   uint32_t file_will_used_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
   ASSERT(file_will_used_blocks <= 140);

    //判断是否要增加块来存储数据
    uint32_t add_blocks = file_will_used_blocks - file_has_used_blocks;
    //无增量，不用申请块
    if(add_blocks == 0) {
        if(file_will_used_blocks <= 12) {
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        } else {
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk,indirect_block_table,all_blocks+12,1);
        }
    } else {//有增量，需要增加块
        //分为三种情况
        //first situation
        if(file_will_used_blocks <= 12) {
            //先将已经使用的最后一个扇区存入allblocks中
            block_idx = file_has_used_blocks-1;
            ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
            //然后指向新的块
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_used_blocks)
            {
                block_lba = block_bitmap_alloc(cur_part);
                
                if(block_idx == -1) {
                    printk("file_write: block_bitmap_alloc for situation 1 failed!\n");
                    return -1;
                }

                ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx] = block_lba;
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                /*must sync block bitmap since we get a block*/
                bitmap_sync(cur_part,block_bitmap_idx,BLOCK_BITMAP);
                /*now assign next block for all_block to store data*/
                block_idx++;
            }
        } else if(file_has_used_blocks <= 12 && file_will_used_blocks > 12) {
            //先将已经使用并且有可能继续用的最后一个扇区存入allblocks中
            block_idx = file_has_used_blocks-1;
            ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
            
            block_lba = block_bitmap_alloc(cur_part);
            if(block_lba == -1) {
                printk("file_write: block_bitmap_alloc for situation 2\n");
                return -1;
            }
            ASSERT(file->fd_inode->i_sectors[12] == 0);
            //创建的一级间接块表
            indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;

            block_idx = file_has_used_blocks;

            while (block_idx < file_will_used_blocks)
            {
                block_lba = block_bitmap_alloc(cur_part);
                if(block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 2\n");
                    return -1;
                }   
                if(block_idx < 12) {
                    ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                    all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx] = block_lba;
                } else {
                    //大于12的同步到all_blocks，循环完之后一起同步
                    all_blocks[block_idx] = block_lba;
                }
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part,block_bitmap_idx,BLOCK_BITMAP);
                block_idx ++;
            }
            //将间接表同步到硬盘
            ide_write(cur_part->my_disk,indirect_block_table,all_blocks+12,1);
            
        }
        else if(file_has_used_blocks > 12){
            ASSERT(file->fd_inode->i_sectors[12] != 0);

            indirect_block_table = file->fd_inode->i_sectors[12];
            //先读，方便后面写入的时候数据同步
            ide_write(cur_part->my_disk,indirect_block_table,all_blocks+12,1);
            while(block_idx < file_will_used_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if(block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 2\n");
                    return -1;
                }
                all_blocks[block_idx] = block_lba;
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part,block_bitmap_idx,BLOCK_BITMAP);
                block_idx++;
            }
            ide_write(cur_part->my_disk,indirect_block_table,all_blocks+12,1);
        }
    }

    uint32_t byte_written = 0;//已经写入的字节
    uint32_t sec_lba = 0;//每次写的扇区号
    uint32_t sec_off_bytes = 0;//
    uint32_t _count = count;
    uint32_t sec_right_bytes = 0;//
    uint32_t sec_idx = 0;//定位每次写扇区的all_blocks index
    uint32_t chunk_size = 0;//每次写入的数据字节
    bool first_write_block = true;
    file->fd_pos = file->fd_inode->i_size - 1;
    while(byte_written < count) {
        memset(io_buf,0,BLOCK_SIZE);//clear buf each times
        sec_idx = file->fd_inode->i_size / BLOCK_SIZE;//向下取整
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
        sec_right_bytes = BLOCK_SIZE - sec_off_bytes;
        chunk_size = _count < sec_right_bytes ? _count : sec_right_bytes;
        if(first_write_block) {
            //先读，目地是同步数据，防止已经在硬盘上的数据丢失，sec_right_bytes之前的
            ide_read(cur_part->my_disk,sec_lba,io_buf,1);
            first_write_block = false;
        }
        memcpy(io_buf+sec_off_bytes,src,chunk_size);
        ide_write(cur_part->my_disk,sec_lba,io_buf,1);
        src += chunk_size;
        file->fd_inode->i_size += chunk_size;
        file->fd_pos += chunk_size;
        byte_written += chunk_size;
        _count -= chunk_size;
    } 
    memset(io_buf,1,BLOCK_SIZE);
    inode_sync(cur_part,file->fd_inode,io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);
    return byte_written;
}

int32_t file_read(struct file* file, void* buf, uint32_t count) {
    uint8_t* buf_dst = buf;
    uint32_t size = count, size_right = count;

    /*每读一次文件就会将fd-》pos改变，因为用户有可能多次调用read函数
     *打开文件的时候file->pos已经被置
     *如果读取的次数加上偏移量大于文件的大小
     *用剩余量代表读取的字节数  */
    if((file->fd_pos + count) > file->fd_inode->i_size) {
        size = file->fd_inode->i_size - file->fd_pos;
        size_right = size;
        //证明读到文件末尾
        if(size == 0) {
            return -1;
        }
    }

    uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
    if(io_buf == NULL) {
        printk("file_read: sys_malloc for io_buf failed\n");
        return -1;
    }
    uint32_t* all_blocks = sys_malloc(BLOCK_SIZE + 48);
    if(all_blocks == NULL) {
        printk("file_read: sys_malloc for all_blocks failed\n");
        return -1;
    }

    //计算此次需要读入的起始块下标,而write函数计算的是块的数量
    uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;
    //计算此次需要读入的终止块下标
    uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;

    uint32_t read_blocks = block_read_end_idx - block_read_start_idx;
    
    int32_t indirect_block_table;//间接表的扇区号
    uint32_t block_idx;//记录块下标
    //若增量为0
    if(read_blocks == 0) {
        if(block_read_end_idx < 12) {
            block_idx = block_read_end_idx;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        } else {
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk,indirect_block_table,all_blocks+12,1);
        }
    } else {
        if(block_read_end_idx < 12) {
            block_idx = block_read_start_idx;
            while(block_idx <= block_read_end_idx) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
        } else if(block_read_start_idx < 12 && block_read_end_idx >= 12) {
            block_idx = block_read_start_idx;
            while(block_idx <= 12) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
            //判断inode的简介块不为0
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk,indirect_block_table,all_blocks+12,1);
        } else {
            //判断inode的简介块不为0
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk,indirect_block_table,all_blocks+12,1);
        }
    }

    uint32_t sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
    uint32_t bytes_read = 0;
    while(bytes_read < size) {
        //计算每次读取的块下标
        sec_idx = file->fd_pos / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        //由于计算此次读取的扇区中已经读取的数据
        sec_off_bytes = file->fd_pos % BLOCK_SIZE;
        //未读取的数据
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
        chunk_size = size_right < sec_left_bytes ? size_right : sec_left_bytes;
        //if you are wary, you can steer boat forever
        memset(io_buf,0,BLOCK_SIZE);
        ide_read(cur_part->my_disk,sec_lba,io_buf,1);
        memcpy(buf_dst,io_buf+sec_off_bytes,chunk_size);

        buf_dst += chunk_size;
        file->fd_pos += chunk_size;
        bytes_read += chunk_size;
        size_right -= chunk_size;
    }
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_read;
}







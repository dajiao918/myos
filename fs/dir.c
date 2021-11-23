#include "dir.h"
#include "stdint.h"
#include "global.h"
#include "ide.h"
#include "memory.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "string.h"
#include "debug.h"
#include "file.h"
#include "super_block.h"

struct dir root_dir;

/*根据分区中的超级快打开根目录*/
void open_root_dir(struct partition* part) {
    root_dir.inode = inode_open(part,part->sb->root_inode_no);
    root_dir.dir_pos = 0;
}
/*在part分区上打开i节点为inode_no的目录并返回目录指针*/
struct dir* dir_open(struct partition* part, uint32_t inode_no) {
    struct dir* pdir = (struct dir*) sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part,inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

/*在part分区内的pdir目录内寻找名为name的文件或目录
 *找到后返回true并将其目录存入dir_e，否则返回false */
bool search_dir_entry(struct partition* part,struct dir* pdir,
                    const char* name, struct dir_entry* dir_e) {
    uint32_t block_cnt = 12 + 128;
    //12*4 + 128*4
    uint32_t* all_blocks = (uint32_t*)sys_malloc(48+512);
    if(all_blocks == NULL) {
        printk("search_dir_entry: sys_malloc for all_blocks failed");
        return false;
    }

    uint32_t block_idx = 0;
    while(block_idx < 12) {
        all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx ++;
    }

    block_idx = 0;
    if(pdir->inode->i_sectors[12] != 0){
        ide_read(part->my_disk,pdir->inode->i_sectors[12],
        &all_blocks[12],1);
    }
    //现在all_blocks存入了pdir所有的文件的扇区地址

    uint8_t* buf = (uint8_t*)sys_malloc(512);
    //每次根据all_blocks的地址读取扇区，然后由p_de获取文件名，与传入的参数比较
    struct dir_entry* p_de = (struct dir_entry*)buf;

    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = 512/dir_entry_size;
    //每个扇区可以容纳的目录个数
    while(block_idx < block_cnt) {
        //由于目录是可以被删除的，所以前面的快地址没有存储数据
        //不代表后面的块地址也没有存储数据
        if(all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk,all_blocks[block_idx],buf,1);
        uint32_t dir_entry_idx = 0;
        while(dir_entry_idx < dir_entry_cnt) {
            //如果文件名相同，证明找到了
            if(!strcmp(p_de->filename,name)) {
                memcpy(dir_e,p_de,dir_entry_size);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            p_de++;
            dir_entry_idx++;
        }
        block_idx++;
        //initialize p_de repetitively
        p_de = (struct dir_entry*)buf;
        memset(buf,0,512);
    }
    sys_free(buf);
    sys_free(all_blocks);
    return false;
}

/*关闭目录实质上就是释放目录和inode所占的内存*/
void dir_close(struct dir* dir) {

    if(dir == &root_dir) {
        return;
    }
    inode_close(dir->inode);
    sys_free(dir);
}

void create_dir_entry(char* filename, uint32_t inode_no, uint8_t file_type,struct dir_entry* p_de) {
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);
    memcpy(p_de->filename,filename,strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}

/*将目录项p_de写入父目录parent_dir中*/
bool sync_dir_entry(struct dir* parent_dir,
                    struct dir_entry* p_de,void* io_buf) {
    
    struct inode* dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    ASSERT(dir_size%dir_entry_size==0);

    uint32_t dir_entrys_per_sec = (512/dir_entry_size);

    int32_t block_lba = -1;
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0};

    while(block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    if(dir_inode->i_sectors[12] != 0){
        ide_read(cur_part->my_disk,dir_inode->i_sectors[12],
        all_blocks + 12,1);
    }

    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    int32_t block_bitmap_idx = -1;

    block_idx = 0;
    while(block_idx < 140) {
        block_bitmap_idx = -1;
        //先读再写，书上并没有读
        ide_read(cur_part->my_disk,all_blocks[block_idx],io_buf,1);
        if(all_blocks[block_idx] == 0) {
            block_lba = block_bitmap_alloc(cur_part);
            //printk("sync_dir_entry: inode %d alloc block_lba is 0x%x\n",parent_dir->inode->i_no,block_lba);

            if(block_lba == -1 ){
                printk("alloc block bitmap for sync_dir_entry failed!");
                return false;
            }
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(cur_part,block_bitmap_idx,BLOCK_BITMAP);
            block_bitmap_idx = -1;
            if(block_idx < 12) {
                dir_inode->i_sectors[block_idx] = 
                all_blocks[block_idx] = block_lba;
            } else if(block_idx == 12 && all_blocks[12] == 0) {
                //作为间接块地址
                dir_inode->i_sectors[12] = block_lba;
                block_lba = -1;
                block_lba = block_bitmap_alloc(cur_part);
                if(block_lba = -1) {
                    //回滚刚才分配的块
                    block_bitmap_idx = dir_inode->i_sectors[12]-
                    cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap,block_bitmap_idx,0);
                    dir_inode->i_sectors[12] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed!\n");
                    return false;
                }
                all_blocks[12] = block_lba;
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part,block_bitmap_idx,BLOCK_BITMAP);
                ide_write(cur_part->my_disk,dir_inode->i_sectors[12],all_blocks+12,1);
            } else {
                all_blocks[block_idx] = block_lba;
                ide_write(cur_part->my_disk,dir_inode->i_sectors[12],all_blocks+12,1);
            }
            //上面的工作给当前没有分配块的直接快或者简介块分配块
            //现在的工作才是主要目的，同步新目录项
            // memset(io_buf,0,512);
            memcpy(io_buf,p_de,dir_entry_size);
            ide_write(cur_part->my_disk,all_blocks[block_idx],io_buf,1);
            return true;
        }

        
        //如果block_idx已经存在块，那么读入当前块的信息，遍历整个块
        //如果有空目录项，就写入
        //printk("sync_dir_entry: inode %d has had a block block_lba is 0x%x\n",parent_dir->inode->i_no,all_blocks[block_idx]);
        uint8_t dir_entry_idx = 0;
        while(dir_entry_idx < dir_entrys_per_sec) {
            if((dir_e + dir_entry_idx)->f_type == FT_UNKOWN) {
                
                memcpy(dir_e+dir_entry_idx,p_de,dir_entry_size);
                ide_write(cur_part->my_disk,all_blocks[block_idx],io_buf,1);
                dir_inode->i_size += dir_entry_size;
                return true;
            } //else {
            //     printk("%d dir or file name is %s\n",dir_entry_idx,(dir_e+dir_entry_idx)->filename);
            // }
            dir_entry_idx++;
        }
        block_idx++;
    }
    //目录已经满了
    printk("directory is full");
    return false;
}

/*把分区目录pdir中编号为inode_no的目录项删除*/
bool delete_dir_entry(struct partition* part, struct dir* pdir, uint32_t inode_no, void* io_buf) {
    struct inode* pdir_inode = pdir->inode;

    // printk("begin to delete_dir_entry\n");
    uint32_t block_idx = 0, all_blocks[140] = {0},block_cnt = 12;

    while(block_idx < 12) {
        all_blocks[block_idx] = pdir_inode->i_sectors[block_idx];
        block_idx++;
    }

    if(pdir_inode->i_sectors[12] != 0) {
        ide_read(part->my_disk,pdir_inode->i_sectors[12],
        all_blocks+12,1);
        block_cnt = 140;
    }

    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_per_sec = (SECTOR_SIZE / dir_entry_size);
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    struct dir_entry* dir_entry_found = NULL;
    uint8_t dir_entry_idx, dir_entry_cnt;
    bool is_dir_first_block = false;

    block_idx = 0;
    while(block_idx < block_cnt) {
        is_dir_first_block = false;
        if(all_blocks[block_idx] == 0) {
            block_idx ++;
            continue;
        }

        dir_entry_idx = dir_entry_cnt = 0;
        memset(io_buf,0,SECTOR_SIZE);
        ide_read(part->my_disk,all_blocks[block_idx],io_buf,1);
        while(dir_entry_idx < dir_entry_per_sec) {
            if((dir_e + dir_entry_idx)->f_type != FT_UNKOWN) {

                if( !strcmp( (dir_e + dir_entry_idx)->filename , "." )) {
                    is_dir_first_block = true;
                } 
                else if(strcmp( (dir_e + dir_entry_idx)->filename , "." ) &&
                    strcmp( (dir_e + dir_entry_idx)->filename , ".." )) {
                    
                    dir_entry_cnt ++;
                    if( (dir_e + dir_entry_idx)->i_no == inode_no  ) {
                        ASSERT(dir_entry_found == NULL);
                        dir_entry_found = dir_e + dir_entry_idx;
                    }
                }
            }
            dir_entry_idx ++;
        }

        if(dir_entry_found == NULL) {
            block_idx ++;
            continue;
        }

        ASSERT(dir_entry_cnt >= 1);
        //该目录项独占一个扇区，且不是地一个扇区
        if(dir_entry_cnt == 1 && !is_dir_first_block) {
            /*1. 在块位图中回收块*/
            uint32_t block_bitmap_idx = 
            all_blocks[block_idx] - part->sb->data_start_lba;
            bitmap_set(&part->block_bitmap,block_bitmap_idx,0);
            bitmap_sync(part,block_bitmap_idx,BLOCK_BITMAP);
            /*2. 将块地址从数组i-selectors中去掉*/
            if(block_idx < 12) {
                pdir_inode->i_sectors[block_idx] = 0;
            } else {
                //块存储在一级间接表中
                uint32_t indirect_blocks = 0;
                uint32_t indirect_blocks_idx = 12;
                while (indirect_blocks_idx < 140) {
                    if(all_blocks[indirect_blocks_idx] != 0) {
                        indirect_blocks ++;
                    }
                }
                ASSERT(indirect_blocks >= 1);

                if(indirect_blocks > 1) {
                    all_blocks[block_idx] = 0;
                    ide_write(part->my_disk, pdir_inode->i_sectors[12], all_blocks+12,1);

                } else {
                    /*间接表只有一个数据块，全部回收*/
                    block_bitmap_idx = pdir_inode->i_sectors[12] - part->sb->data_start_lba;
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                    bitmap_sync(part,block_bitmap_idx,BLOCK_BITMAP);
                    pdir_inode->i_sectors[12] = 0;
                }
            }
        } else {
            /*该扇区上还有其他的目录, found 指向io_buf*/
            memset(dir_entry_found,0,dir_entry_size);
            ide_write(part->my_disk,all_blocks[block_idx],io_buf,1);
        }
        ASSERT(pdir_inode->i_size >= dir_entry_size);
        pdir_inode->i_size -= dir_entry_size;
        memset(io_buf,0,SECTOR_SIZE*2);
        inode_sync(part,pdir_inode,io_buf);
        return true;
    }
    return false;
}

/*读取目录，每次读取一个目录项，
 *每次读取之后都会改变dir里面的dir-pos
 *便于下次读取的时候不会读取的已经读取过的目录项
*/
struct dir_entry* dir_read(struct dir* dir) {
    struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;
    struct inode* dir_inode = dir->inode;
    uint32_t all_blocks[140] = {0}, block_idx = 0;
    while(block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx ++;
    }

    if(dir_inode->i_sectors[12] != 0) {
        uint32_t indirect_block = dir_inode->i_sectors[12];
        ide_read(cur_part->my_disk,indirect_block,all_blocks+12,1);
    }
    block_idx = 0;

    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entry_cnt_per_sec = SECTOR_SIZE / dir_entry_size;
    uint32_t dir_entry_idx = 0;
    uint32_t cur_pos = 0;
    while(dir->dir_pos < dir->inode->i_size) {
        if(all_blocks[block_idx] == 0) {
            block_idx ++;
            continue;
        }
        memset(dir_e,0,SECTOR_SIZE);
        ide_read(cur_part->my_disk,all_blocks[block_idx],dir_e,1);
        while(dir_entry_idx < dir_entry_cnt_per_sec) {
            if((dir_e + dir_entry_idx)->f_type != FT_UNKOWN) {
                
                if(cur_pos < dir->dir_pos) {
                    cur_pos += dir_entry_size;
                    dir_entry_idx++;
                    continue;
                }
                ASSERT(cur_pos == dir->dir_pos);
                dir->dir_pos += dir_entry_size;
                return dir_entry_idx+dir_e;
            }
            dir_entry_idx ++;   
        }
        block_idx ++;
    } 
    return NULL;
}


bool dir_is_empty(struct dir* dir) {
    return (dir->inode->i_size != cur_part->sb->dir_entry_size * 2);
}

int32_t dir_remove(struct dir* parent_dir, struct dir* child_dir) {

    struct inode* child_dir_inode = child_dir->inode;
    printk("child_dir inode_no is %d\n",child_dir_inode->i_no);
    int block_idx = 1;
    while(block_idx < 13) {
        ASSERT(child_dir_inode->i_sectors[block_idx] == 0);
        block_idx++;
    }
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);

    if(io_buf == NULL) {
        printk("dir_remove: sys_malloc for io_buf failed\n");
        return -1;
    }

    /*delete the child_dir entry in the parent_dir*/
    delete_dir_entry(cur_part,parent_dir,child_dir_inode->i_no,io_buf);
    /*recly the sector int the i-selecotrs int the child_indoe*/
    inode_release(cur_part,child_dir_inode->i_no);
    sys_free(io_buf);    
    return 0;
}
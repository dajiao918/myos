#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "file.h"
#include "console.h"
#include "keyboard.h"
#include "ioqueue.h"

struct partition* cur_part;	 // 默认情况下操作的是哪个分区

/*挂载硬盘，也就是读取硬盘信息到内存*/
static bool mount_partition(struct list_elem* pelem, int arg) {
    char* part_name = (char*) arg;
    struct partition* part = elem2entry(struct partition,part_tag,pelem);

    if(!strcmp(part->name,part_name)) {
        cur_part = part;
        struct disk* hd = cur_part->my_disk;
        //用来存储从硬盘上读入的超级快
        struct super_block* sb = (struct super_block*) \
        sys_malloc(SECTOR_SIZE);
        
        cur_part->sb = (struct super_block*)\
        sys_malloc(SECTOR_SIZE);

        if(cur_part->sb == NULL) {
            PANIC("alloc memory failed!");
        }

        memset(sb,0,SECTOR_SIZE);
        ide_read(hd,cur_part->start_lba+1,sb,1);

        memcpy(cur_part->sb,sb,sizeof(struct super_block));

        cur_part->block_bitmap.bits = \
        (uint8_t*) sys_malloc(sb->block_bitmap_sects*SECTOR_SIZE);
        
        if(cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }

        cur_part->block_bitmap.btmp_bytes_len = sb->block_bitmap_sects*SECTOR_SIZE;

        ide_read(hd,sb->block_bitmap_lba,\
        cur_part->block_bitmap.bits,sb->block_bitmap_sects);

        //申请位图的内存，bitmap_sects*SECTOR_SIZE
        cur_part->inode_bitmap.bits = \
        (uint8_t*) sys_malloc(sb->inode_bitmap_sects*SECTOR_SIZE);

        if(cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }


        cur_part->inode_bitmap.btmp_bytes_len = sb->inode_bitmap_sects * SECTOR_SIZE;

        ide_read(hd,sb->inode_bitmap_lba,\
        cur_part->inode_bitmap.bits,sb->inode_bitmap_sects);

        list_init(&cur_part->open_inodes);

        printk("block_bitmap_lba: 0x: %x\n",cur_part->sb->block_bitmap_lba);
        printk("inode_bitmap_lba: 0x:%x\n",cur_part->sb->inode_bitmap_lba);
        printk("inode_table_lba: 0x%x\n",cur_part->sb->inode_table_lba);
        printk("root_dir_lba:0x%x\n",cur_part->sb->data_start_lba);
        printk("mount %s done\n",cur_part->name);
        return true;
    }
    return false;
}

/*格式话分区，创建超级块，inode等到硬盘*/
static void partition_format(struct partition* part) {
    uint32_t boot_sectors_sects = 1;
    uint32_t super_block_sects = 1;
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART,BITS_PER_SECTOR);
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode)) * MAX_FILES_PER_PART),SECTOR_SIZE);
    uint32_t used_sects = boot_sectors_sects + super_block_sects + \
    inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;

    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects,BITS_PER_SECTOR);
    uint32_t bit_map_byte_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(bit_map_byte_len,BITS_PER_SECTOR);

    struct super_block sb;
    sb.magic = 0x12345678;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;
    
    sb.block_bitmap_lba = part->start_lba + 2;
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);

    printk("%s info:\n", part->name);
    printk(" magic:0x%x\n part_lba_base:0x%x\n \
        all_sectors:0x%x\n inode_cnt:0x%x\nblock_bitmap_lba:0x%x\n \
        block_bitmap_sectors:0x%x\n inode_bitmap_lba:0x%x\n \
        inode_bitmap_sectors:0x%x\ninode_table_lba:0x%x\n \
        inode_table_sectors:0x%x\ndata_start_lba:0x%x\n", \
        sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt,
        sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba,
        sb.inode_bitmap_sects, sb.inode_table_lba,
        sb.inode_table_sects, sb.data_start_lba);


    struct disk* hd = part->my_disk;
    ide_write(hd,part->start_lba+1,&sb,1);
    printk("    super_block_lba:0x%x\n", part->start_lba+1);

    uint32_t buf_size = sb.block_bitmap_sects > sb.inode_bitmap_sects ? \
    sb.block_bitmap_sects : sb.inode_bitmap_sects;
    buf_size = (buf_size > sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t* buf = (uint8_t*) sys_malloc(buf_size);

    //reserved by root directory
    buf[0] = 0x01;
    //the last byte of block_bitmap
    uint32_t block_bitmap_last_byte = bit_map_byte_len / 8;
    //the last bit of block_bitmap
    uint8_t block_bitmap_last_bit = bit_map_byte_len % 8;

    //获取块位图占据的最后一个扇区中没有使用的部分，次部分应该全部置1,防止被申请使用
    uint32_t last_size = (SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE));

    memset(&buf[block_bitmap_last_byte],0xff,last_size);

    uint8_t bit_idx = 0;
    while(bit_idx <= block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx);
        bit_idx ++;
    }
    ASSERT(sb.block_bitmap_sects > 0);
    ide_write(hd,sb.block_bitmap_lba,buf,sb.block_bitmap_sects);

    memset(buf,0,buf_size);

    buf[0] = 0x01;
    ASSERT(sb.inode_bitmap_sects > 0);
    ide_write(hd,sb.inode_bitmap_lba,buf,sb.inode_bitmap_sects);

    memset(buf,0,buf_size);
    struct inode* i = (struct inode*)buf;
    i->i_size = sb.dir_entry_size*2; //. and ..
    i->i_sectors[0] = sb.data_start_lba;
    i->i_no = 0;

    ASSERT(sb.inode_table_sects > 0);
    ide_write(hd,sb.inode_table_lba,buf,sb.inode_table_sects);

    // write . and .. directory to start_data_lba
    memset(buf,0,buf_size);

    struct dir_entry* dir = (struct dir_entry*)buf;

    dir->i_no = 0;
    memcpy(dir->filename,".",1);
    dir->f_type = FT_DIRECTORY;
    dir++;

    dir->i_no = 0;
    memcpy(dir->filename,"..",2);
    dir->f_type = FT_DIRECTORY;
    ide_write(hd,sb.data_start_lba,buf,1);

    printk("    root_dir_lba:0x%x\n",sb.data_start_lba);
    printk("%s format donw\n",part->name);
    sys_free(buf);
}
/*解析路径，例如：
    ///a/b   name_store存入a
    返回值返回 /b
*/
char* path_parse(char* pathname, char* name_store){
    //leap '/' char such as '//a/b->a/b'
    while(*pathname == '/') {
        pathname++;
    }    

    while(*pathname != '/' && *pathname != 0) {
        *name_store = *pathname;
        name_store++;
        pathname++;
    }
    if(pathname[0] == 0) {
        return NULL;
    }
    return pathname;
}

int32_t path_depth_cnt(char* pathname) {
    ASSERT(pathname != NULL);
    char* p = pathname;
    uint32_t deepth = 0;
    char name_store[MAX_FILE_NAME_LEN];
    p = path_parse(p,name_store);

    while(name_store[0]) {
        deepth++;
        memset(name_store,0,MAX_FILE_NAME_LEN);
        if (p)
        {
            p = path_parse(p,name_store);
        }
        
    }
    return deepth;
}
/*搜索文件pathname，若找到则返回其inode号，否则返回-1*/
static int search_file(const char* pathname,
struct path_search_record* searched_record) {
    if(!strcmp(pathname,"/") || !strcmp(pathname,"/.")
    || !strcmp(pathname,"/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;
        return 0;
    }

    uint32_t path_len = strlen(pathname);

    ASSERT(pathname[0] == '/' && path_len > 1 &&
    path_len < MAX_PATH_LEN);

    char* sub_path = (char*) pathname;
    struct dir* parent_dir = &root_dir;
    struct dir_entry dir_e;

    char name[MAX_FILE_NAME_LEN] = {0};

    searched_record->parent_dir = parent_dir;
    searched_record->file_type = FT_UNKOWN;
    uint32_t parent_inode_no = 0;

    sub_path = path_parse(sub_path,name);

    while(name[0]) {

        strcat(searched_record->searched_path,"/");
        strcat(searched_record->searched_path,name);

        if(search_dir_entry(cur_part,parent_dir,name,&dir_e)) {

            memset(name,0,MAX_FILE_NAME_LEN);
            if(sub_path) {
                //解析下层路径
                sub_path = path_parse(sub_path,name);
            }

            if(dir_e.f_type == FT_DIRECTORY) {
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part,dir_e.i_no);
                searched_record->parent_dir = parent_dir;
                continue;
            } else if(FT_REGULAR == dir_e.f_type) {
                searched_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            } 

        } else {
            return -1;
        }
    }

    //到了这一步，路径被全部遍历完，并且找到了和pathname同名的目录
    //关闭此目录
    dir_close(searched_record->parent_dir);
    //打开次目录的上级目录，作为搜索的父目录
    searched_record->parent_dir = dir_open(cur_part,parent_inode_no);
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}
int32_t sys_open(const char* pathname,uint8_t flags) {

    if(pathname[strlen(pathname)-1] == '/') {
        printk("can't open a directory%s\n",pathname);
        return -1;
    }

    ASSERT(flags < 7);
    int32_t fd = -1;
    struct path_search_record search_record;
    memset(&search_record,0,sizeof(struct path_search_record));
    uint32_t deepth = path_depth_cnt((char*)pathname);

    int inode_no = search_file(pathname,&search_record);
    bool found = inode_no != -1?true:false;

    //遍历玩了整个路径，得到的是一个目录
    if(search_record.file_type == FT_DIRECTORY) {
        printk("can't open a directory with open(), please use opendir instead!\n");
        dir_close(search_record.parent_dir);
        return -1;
    }

    uint32_t path_search_depth = 
    path_depth_cnt(search_record.searched_path);

    //如果说搜索路径过程中搜索到了最后，并且发现是文件
    //那么搜索过的路径深度和pathname的深度是一样的
    //如果两个深度不一样，证明在中间的时候就失败了
    if(deepth != path_search_depth) {
        printk("cant access %s:Not a directory ,subpath %s isn't exies\n",
        pathname,search_record.searched_path);
        dir_close(search_record.parent_dir);
    }

    //到了这一步，证明搜索路径相等，路径是正确的
    if(!found && !(flags & O_CREAT)) {
        //没有此文件，不能打开
        printk("in path %s, file %s isn't exist\n",
        pathname,(strrchr(search_record.searched_path,'/')+1));
        dir_close(search_record.parent_dir);
        return -1;
    } else if(found && (flags & O_CREAT)) {
        //文件已经存在
        printk("%s has already exist!\n", pathname);
        dir_close(search_record.parent_dir);
        return -1;
    }

    switch(flags & O_CREAT) {
        case O_CREAT:
            printk("createing file..");
            fd = file_create(search_record.parent_dir,(strrchr(pathname, '/') + 1), flags);
            dir_close(search_record.parent_dir);
            break;
        default:
            //其他情况就是打开文件
            // printk("open the file %s\n",pathname);
            fd = file_open(inode_no,flags);
    }
    return fd;
}

static uint32_t fdlocal2global(uint32_t local_fd) {
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

int32_t sys_close(int32_t fd) {
    int32_t ret = -1;
    if(fd > 2) {
        uint32_t _fd = fdlocal2global(fd);
        ret = file_close(&file_table[_fd]);
        running_thread()->fd_table[fd] = -1;
    }
    return ret;
} 

int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
    if(fd < 0) {
        printk("sys_write: fd error\n");
        return -1;
    }
    if(fd == stdout_no) {
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf,buf,count);
        console_put_str(tmp_buf);
        return count;
    }

    uint32_t _fd =  fdlocal2global(fd);
    struct file* file = &file_table[_fd];
    if(file->fd_flag & O_WRONLY || file->fd_flag & O_RDWR) {
        uint32_t byte_written = file_write(file,buf,count);
        return byte_written;
    } else {
        console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
        return -1;
    }
}

int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
    ASSERT(buf != NULL);
    int ret = - 1;
    if(fd < 0 || fd == stdout_no || fd == stderr_no) {
        printk("sys_read: fd error\n");
        ret =  -1;
    } else if(fd == stdin_no) {
        char* buffer = buf;
        uint32_t bytes_read = 0;
        while(bytes_read < count) {
            *buffer = ioq_getchar(&kbd_buf);
            buffer++;
            bytes_read++;
        }
        ret = (bytes_read == 0 ? -1 : (int32_t)bytes_read);
    } else {
        uint32_t _fd = fdlocal2global(fd);
        ret = file_read(&file_table[_fd],buf,count);
    }
    return ret;
}

int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if(fd < 0) {
        printk("sys_leek: fd < 0 error\n");
        return -1;
    }

    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fdlocal2global(fd);
    struct file* file = &file_table[_fd];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)file->fd_inode->i_size;
    switch (whence)
    {
        case SEEK_SAT:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = offset + file->fd_pos;
            break;
        case SEEK_END://offset maybe is a negative number
            new_pos = offset + file_size;
            break;
    }
    if(new_pos< 0 || new_pos >= file_size){
        return -1;
    }
    file->fd_pos = new_pos;
    return new_pos;
}

int32_t sys_unlink(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    struct path_search_record searched_record;
    memset(&searched_record, 0 , sizeof(struct path_search_record));
    int inode_no = search_file(pathname,&searched_record);
    ASSERT(inode_no != 0);
    /*没有这个文件*/
    if(inode_no == -1) {
        printk("file %s not found\n",pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    if(searched_record.file_type == FT_DIRECTORY) {
        printk("can't delete a directory with unlink(),user rmdir() to instead \n");
        return -1;
    }

    /*不能删除已经打开的文件*/
    uint32_t fd_idx = 0;
    while(fd_idx < MAX_FILE_OPEN) {
        if(file_table[fd_idx].fd_inode != NULL && 
        file_table[fd_idx].fd_inode->i_no == (uint32_t)inode_no) {
            break;
        }
        fd_idx ++;
    }

    if(fd_idx < MAX_FILE_OPEN) {
        dir_close(searched_record.parent_dir);
        printk("file %s is in use, not allow to delete!\n",pathname);
        return -1;
    }

    ASSERT(fd_idx == MAX_FILE_OPEN);
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if(io_buf == NULL) {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink: malloc for iobuf failed\n");
        return -1;
    }

    struct dir* parent_dir = searched_record.parent_dir;
    delete_dir_entry(cur_part,parent_dir,inode_no,io_buf);
    printk("delete success!\n");
    printk("now to release inode_no %d selectors",inode_no);
    inode_release(cur_part,inode_no);
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;
}

/*创建目录pathname
* 1.解析pathname
  2.分配inode
  3.创建目录项 . & ..
  4.为父目录创建目录项--syn_dir_entry
  5.同步父目录inode，当前目录inode
*/
int32_t sys_mkdir(const char* pathname) {
    uint8_t rollback_step = 0;

    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if(io_buf == NULL) {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    struct path_search_record search_record;
    memset(&search_record,0,sizeof(struct path_search_record));
    int inode_no = -1;

    inode_no = search_file(pathname,&search_record);
    if(inode_no != -1) {
        printk("sys_mkdir: file or directory %s exist!\n",pathname);
        rollback_step = 1;
        goto rollback;
    } else {
        int pathname_cnt = path_depth_cnt((char*)pathname);
        int search_cnt = path_depth_cnt(search_record.searched_path);
        if(pathname_cnt != search_cnt) {
            printk("sys_mkdir: cannot access %s: NOT a directory, \
            subpath %s isn't exist\n",pathname,search_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }

    }

    struct dir* parent_dir = search_record.parent_dir;
    // printk("sys_mkdir: parent_dir inode is %d\n",parent_dir->inode->i_no);
    //获取目录名
    char* dirname = strrchr(search_record.searched_path,'/') + 1;
    inode_no = inode_bitmap_alloc(cur_part);
    if(inode_no == -1) {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    struct inode new_dir_inode;
    inode_init(inode_no,&new_dir_inode);

    uint32_t block_bitmap_idx = 0;
    int32_t block_lba = -1;
    block_lba = block_bitmap_alloc(cur_part);
    if(block_lba == -1) {
        printk("sys_mkdir:block_bitamp_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_lba;
    // printk("sys_mkdir: alloc block_lba is 0x%x\n", block_lba);
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part,block_bitmap_idx,BLOCK_BITMAP);

    memset(io_buf,0,SECTOR_SIZE*2);

    struct dir_entry* p_de = (struct dir_entry*)io_buf;

    memcpy(p_de->filename,".",1);
    p_de->f_type = FT_DIRECTORY;
    p_de->i_no = inode_no;
    // printk("sys_mkdir: p_de name is %s dir inode_no is%d\n",p_de->filename,p_de->i_no);
    p_de++;
    memcpy(p_de->filename,"..",2);
    p_de->f_type = FT_DIRECTORY;
    p_de->i_no = parent_dir->inode->i_no;
    // printk("sys_mkdir: p_de name is %s dir inode_no is%d\n",p_de->filename,p_de->i_no);
    ide_write(cur_part->my_disk,new_dir_inode.i_sectors[0],io_buf,1);
    new_dir_inode.i_size += 2 * cur_part->sb->dir_entry_size;

    struct dir_entry new_dir_entry;
    memset(&new_dir_entry,0,sizeof(struct dir_entry));
    create_dir_entry(dirname,inode_no,FT_DIRECTORY,&new_dir_entry);

    if(!sync_dir_entry(parent_dir,&new_dir_entry,io_buf)) {
        printk("sys_mkdir:sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part,parent_dir->inode,io_buf);
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part,&new_dir_inode,io_buf);

    bitmap_sync(cur_part,inode_no,INODE_BITMAP);
    sys_free(io_buf);
    dir_close(search_record.parent_dir);
    return 0;

rollback:
    switch (rollback_step)
    {
    case 2:
        bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        break;
    case 1:
        dir_close(search_record.parent_dir);
        break;
    }
    sys_free(io_buf);
    return -1;
}

/*打开目录，本质上就是加载硬盘中的inode信息进入内存*/
struct dir* sys_opendir(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    if(pathname[0] == '/' && (pathname[1] == '.' || pathname[1] == 0)) {
        return &root_dir;
    }

    struct path_search_record search_record;
    int inode_no = search_file(pathname,&search_record);
    struct dir* ret = NULL;
    if(inode_no == -1) {
        printk("In %s,subpath %s not exist\n",pathname,search_record.searched_path);
    } else {
        if(search_record.file_type == FT_REGULAR) {
            printk("%s is a regular file!\n",pathname);
        } else if(search_record.file_type == FT_DIRECTORY) {
            ret = dir_open(cur_part,inode_no);
        }
    }
    dir_close(search_record.parent_dir);
    return ret;
}

/*关闭目录*/
int32_t sys_closedir(struct dir* dir) {
    int32_t ret = -1;
    if(dir != NULL) {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

/*读取目录的一个dir，间接调用dir_read...*/
struct dir_entry* sys_readdir(struct dir* dir) {
    ASSERT(dir != NULL);
    return dir_read(dir);
}

/*将目录的dis->pos置为0*/
void sys_rewindir(struct dir* dir) {
    dir->dir_pos = 0;
}


int32_t sys_rmdir(const char* filename) {

    struct path_search_record search_record;
    memset(&search_record,0,sizeof(struct path_search_record));
    int inode_no = search_file(filename,&search_record);
    int retval = -1;
    if(inode_no == -1) {
        printk("In %s, sub path %s not exist\n",filename,search_record.searched_path);
    } else if(search_record.file_type == FT_REGULAR) {
        printk("%s is a file\n",filename);
    } else {
        struct dir* dir  = dir_open(cur_part,inode_no);
        if (dir_is_empty(dir))
        {
            printk("dir %s is not empty, it is not allowed \
            to delete a noneempty directory\n",filename);
        } else {
            if(!dir_remove(search_record.parent_dir,dir)) {
                retval = 0;
            }
        }
        dir_close(dir);
    }
    dir_close(search_record.parent_dir);
    return retval;
}
static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void* io_buf) {
    struct inode* child_inode = inode_open(cur_part,child_inode_nr);
    uint32_t block_lba = child_inode->i_sectors[0];
    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_inode);
    ide_read(cur_part->my_disk,block_lba,io_buf,1);
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    //dir_e[0] is . dir_e[1] is ..
    ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == FT_DIRECTORY);
    return dir_e[1].i_no;
}

static int get_child_dir_name(uint32_t p_inode_nr, uint32_t c_inode_nr,
char* path, void* io_buf) {
    struct inode* parent_inode = inode_open(cur_part, p_inode_nr);
    uint32_t block_idx = 0, block_cnt = 12;
    uint32_t all_blocks[140] = {0}; 
    while(block_idx < block_cnt) {
        all_blocks[block_idx] = parent_inode->i_sectors[block_idx];
        block_idx ++;
    }

    if(parent_inode->i_sectors[12] != 0) {
        block_cnt = 140;
        ide_read(cur_part->my_disk,parent_inode->i_sectors[12],
        all_blocks+12,1);
    }

    inode_close(parent_inode);

    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;

    block_idx = 0;
    while(block_idx < block_cnt) {
        if(all_blocks[block_idx]) {
            memset(io_buf,0,SECTOR_SIZE);
            ide_read(cur_part->my_disk,all_blocks[block_idx],
            io_buf,1);
            uint32_t dir_idx = 0;
            while(dir_idx < dir_entrys_per_sec) {
                if( (dir_e + dir_idx)->i_no == c_inode_nr ) {
                    strcat(path,"/");
                    strcat(path,(dir_e + dir_idx)->filename);
                    return 0;
                }
                dir_idx++;
            }
        }
        block_idx++;
    }
    return -1;
}

char* sys_getcwd(char* buf, uint32_t size) {
    void* io_buf = sys_malloc(SECTOR_SIZE);
    if(io_buf == NULL) {
        printk("sys_getcwd: alloc io_buf failed\n");
        return NULL;
    }

    struct task_struct* cur_task = running_thread();
    int32_t parent_inode_nr = 0;
    int32_t child_inode_nr = cur_task->cwd_inode_nr;

    ASSERT(child_inode_nr >= 0 && child_inode_nr < 4096);
    if(child_inode_nr == 0) {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }

    memset(buf,0,size);

    char reversal_path[MAX_PATH_LEN] = {0};

    while(child_inode_nr) {
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr,io_buf);
        // printk("sys_getcwd: parent_inode_no is %d\n",parent_inode_nr);
        // printk("sys_getcwd: child_inode_no is %d\n",child_inode_nr);
        int32_t ret = get_child_dir_name(parent_inode_nr,child_inode_nr,reversal_path,io_buf);
        if(ret == -1) {
            printk("sys_getcwd: thread interior path occur error\n");
            sys_free(io_buf);
            return NULL;
        }
        child_inode_nr = parent_inode_nr;
        // printk("cur path is %s\n",reversal_path);
    }
    ASSERT(strlen(reversal_path) < MAX_PATH_LEN);

    //由于reversal_path是从子目录开始拼接的，所以此时这个目录是反的
    char* last_slash;//记录最后一个/
    while(true) {
        last_slash = strrchr(reversal_path,'/');
        if(last_slash == NULL) {
            break;
        }
        uint32_t len = strlen(buf);
        strcat(buf+len,last_slash);
        *last_slash = 0;//将最后一个/变为0作为结束
    }
    sys_free(io_buf);
    return buf;
}

int32_t sys_chdir(const char* path) {
    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record,0,sizeof(struct path_search_record));
    int32_t inode_no = search_file(path,&searched_record);
    if(inode_no != -1) {
        if(searched_record.file_type == FT_DIRECTORY) {
            // printk("current directory inode_no is %d\n",inode_no);
            running_thread()->cwd_inode_nr = inode_no;
            ret = 0;
        } else {
            printk("sys_chdir: %s is a file or other",path);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}
int32_t sys_stat(const char* path, struct stat* buf) {
    if(!strcmp(path,"/") || !strcmp(path,"/.") || !strcmp(path,"/..")) {
        buf->st_filetype = FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->i_size;
        return 0;
    }

    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record,0,sizeof(struct path_search_record));
    int inode_no = search_file(path,&searched_record);
    if(inode_no != -1) {
        struct inode* obj_inode = inode_open(cur_part,inode_no);
        buf->st_filetype = searched_record.file_type;
        buf->st_ino = inode_no;
        buf->st_size = obj_inode->i_size;
        inode_close(obj_inode);
        ret = 0;
    } else {
        printk("sys_stat: %s not found\n",path);
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

/* 向屏幕输出一个字符 */
void sys_putchar(char char_asci) {
   console_put_char(char_asci);
}

void filesys_init(){

    uint8_t channel_no = 0, dev_no, part_idx = 0;
    struct super_block* sb = (struct super_block*) \
    sys_malloc((SECTOR_SIZE));
    if(sb == NULL) {
        PANIC("alloc memory failed!");
    }
    printk("searching filesystem.....\n");
    while(channel_no < channel_cnt) {
        dev_no = 0;
        while(dev_no < 2) {
            if(dev_no == 0) {
                dev_no ++;
                continue;
            }
            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;
            while(part_idx < 12) {
                if(part_idx == 4) {
                    part = hd->logic_parts;
                }
                if(part->sec_cnt != 0) {
                    memset(sb,0,SECTOR_SIZE);

                    ide_read(hd,part->start_lba+1,sb,1);
                    
                    if(sb->magic == 0x12345678) {
                        printk("%s has filesystem...",part->name);
                    } else {
                        printk("formating %s's partition %s.....\n",hd->name,part->name);
                        partition_format(part);
                    }
                }
                part_idx ++;
                part ++;
            }
            dev_no ++;     
        }
        channel_no ++;
    }
 
    sys_free(sb);
    char default_part[8] = "sdb1";
    list_traversal(&partition_list,mount_partition,(int)default_part);
    //open thr root dir
    open_root_dir(cur_part);
    
    //init file_table
    uint32_t fd_idx;
    while(fd_idx < MAX_FILE_OPEN) {
        file_table[fd_idx].fd_inode = NULL;
        fd_idx++;
    }
}
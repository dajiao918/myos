#ifndef __FS_FS_H
#define __FS_FS_H
#include "stdint.h"

#define MAX_FILES_PER_PART 4096
#define BITS_PER_SECTOR 4096
#define SECTOR_SIZE 512
#define BLOCK_SIZE SECTOR_SIZE
#define MAX_PATH_LEN 512

extern struct partition* cur_part;

enum file_types
{
    FT_UNKOWN, //not support file type
    FT_REGULAR, //normal file
    FT_DIRECTORY //directory
};

enum oflags{
    O_RDONLY, //read only
    O_WRONLY,//write only
    O_RDWR,//read and write
    O_CREAT = 4//create
};

/*文件读写的时候的参照偏移*/
enum whence{
    SEEK_SAT = 1,
    SEEK_CUR,
    SEEK_END,
};

/*record 上级 path when we search file*/
struct path_search_record{
    char searched_path[MAX_PATH_LEN];//查找过程中的父路径
    struct dir* parent_dir; //文件或目录所在的直接父目录
    enum file_types file_type;
};

struct stat{
    uint32_t st_ino; //inode number
    uint32_t st_size;//file size
    enum file_types st_filetype;//file type
};

int32_t sys_open(const char* pathname,uint8_t flags);
int32_t path_depth_cnt(char* pathname);
char* path_parse(char* pathname, char* name_store);
void filesys_init(void);
int32_t sys_close(int32_t fd);
int32_t sys_write(int32_t fd, const void* buf, uint32_t count);
int32_t sys_read(int32_t fd, void* buf, uint32_t count);
int32_t sys_unlink(const char* pathname);
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t sys_mkdir(const char* pathname);
struct dir* sys_opendir(const char* pathname);
int32_t sys_closedir(struct dir* dir);
struct dir_entry* sys_readdir(struct dir* dir);
void sys_rewindir(struct dir* dir);
int32_t sys_rmdir(const char* filename);
char* sys_getcwd(char* buf, uint32_t size);
int32_t sys_chdir(const char* path);
int32_t sys_stat(const char* path, struct stat* buf);
void sys_putchar(char char_asci);

#endif

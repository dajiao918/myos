#ifndef _USERPROG_EXEC_H
#define _USERPROG_EXEC_H

#include "stdint.h"
#include "global.h"

extern void intr_exit(void);
typedef uint32_t Elf32_Word, Elf32_Addr,Elf32_Off;
typedef uint16_t Elf32_Half;

/*elf 格式头*/
struct Elf32_Ehdr
{
    unsigned char e_ident[16];//elf的魔术，是否是32、64位，以及大小端字节序
    Elf32_Half e_type;//elf目标文件类型，可冲定位文件，可执行文件，动态共享文件
    Elf32_Half e_machine;//elf文件可在那个机器上执行
    Elf32_Word e_version;//elf的版本
    Elf32_Addr e_entry;//该目标文件的入口地址
    Elf32_Off e_phoff;//程序头表的在文件内的偏移
    Elf32_Off e_shoff;//节头表在文件内的偏移
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;//elf header的大小
    Elf32_Half e_phentsize;//程序头表中每一项段信息Elf32_Phdr的数据大小
    Elf32_Half e_phnum;//程序头表的数量，就是段的个数
    Elf32_Half e_shentsize;//节头表中每一项节信息的数据大小
    Elf32_Half e_shnum;//节头表的长度
    Elf32_Half e_shstrndx;//
};

struct Elf32_Phdr{
    Elf32_Word p_type;////PT_LOAD | PT_DYNAMIC...
    Elf32_Off p_offset;//在文件中的偏移
    Elf32_Addr p_vaddr;//被加载到内存的虚拟地址
    Elf32_Addr p_paddr;//
    Elf32_Word p_filesz;//该段的大小
    Elf32_Word p_memsz;//在内存打大小
    Elf32_Word p_flags;//可读、可写、可执行?
    Elf32_Word p_align;//
};

enum segment_type{
    PT_NULL,//忽略
    PT_LOAD,//可加载程序段
    PT_DYNAMIC,//动态加载信息
    PT_INTERP,//动态加载其名称
    PT_NOTE,//
    PT_SHLIB,
    PT_PHDR
};

int32_t sys_execv(const char* path, const char* argv[]);
#endif
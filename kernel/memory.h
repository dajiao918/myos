#ifndef _KERNEL_MEMORY_H
#define _KERNEL_MEMORY_H
#include "bitmap.h"
#include "stdint.h"
#include "list.h"

//虚拟地址池
struct virtual_addr{
	struct bitmap vaddr_bitmap;//虚拟地址位图
	uint32_t vaddr_start;//起始虚拟线性地址
};

enum pool_flags {
	PF_KERNEL = 1,
	PF_USER = 2
};

struct mem_block{
	struct list_elem free_elem;
};

struct mem_block_desc{
	uint32_t block_size;//arena的块大小
	uint32_t block_per_arena;//the mass count of per arena
	struct list free_list;
};

#define DESC_CNT 7

#define PG_P_1 1
#define PG_P_0 0
#define PG_RW_R 0
#define PG_RW_W 2
#define PG_US_S 0
#define PG_US_U 4

extern struct pool kernel_pool, user_pool;
void mem_init(void);
void* get_kernel_pages(uint32_t pg_cnt);
void* get_user_pages(uint32_t pg_cnt);
void* get_a_page(enum pool_flags pf,uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void block_desc_init(struct mem_block_desc* block_arr);
void* sys_malloc(uint32_t size);
void sys_free(void* ptr);
uint32_t* pde_ptr(uint32_t vaddr);
uint32_t* pte_ptr(uint32_t vaddr);
void* get_a_page_without_opvaddrbitmap(enum pool_flags pf,uint32_t vaddr);
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt);
#endif

#include "memory.h"
#include "print.h"
#include "stdint.h"
#include "bitmap.h"
#include "global.h"
#include "debug.h"
#include "string.h"
#include "thread.h"
#include "sync.h"
#include "list.h"
#include "stdio-kernel.h"

#define PAGE_SIZE 4096

//位图的起始线性地址
#define MEM_BITMAP_BASE 0xc009a000
//内核虚拟地址的起始分配地址
#define K_HEAP_START 0xc0100000

//用于在页目录中获取页表索引
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
//用于在页表中获取物理页索引
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

//声明内存池结构
struct pool{
	struct bitmap pool_bitmap;//管理物理地址的位图
	uint32_t phy_addr_start;//物理地址的起始分配地址
	uint32_t pool_size;//内存池大小
	struct lock lock;
};

struct arena{
	struct mem_block_desc* desc;
	uint32_t cnt;//if the large is true, 
	//the field respent the number of physical page,else is free mem_block
	bool large; 
};
//declare seven sort arena for kernel
struct mem_block_desc k_block_desc[DESC_CNT];

struct pool kernel_pool, user_pool;
struct virtual_addr kernel_vaddr;

//用于获取pf属性的虚拟内存池的pg_cnt个页，并返回其虚拟起始地址，其实也就是申请虚拟内存，获取起始的虚拟地址
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
	uint32_t vaddr_idx_start = 0;
	uint32_t cnt = 0;
	if(pf == PF_KERNEL) {
		int bit_idx = bitmap_scan(&kernel_vaddr.vaddr_bitmap,pg_cnt);
		if(bit_idx == -1) {
			return NULL;
		}
		while(cnt < pg_cnt) {
			bitmap_set(&kernel_vaddr.vaddr_bitmap,bit_idx + cnt++,1);
		}
	    vaddr_idx_start = bit_idx * PAGE_SIZE + kernel_vaddr.vaddr_start;
	} else {
		struct task_struct* cur = running_thread();
		int bit_idx = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap,pg_cnt);
		if(bit_idx == -1) {
			return NULL;
		}
		while(cnt < pg_cnt) {
			bitmap_set(&cur->userprog_vaddr.vaddr_bitmap,bit_idx+cnt,1);
			cnt ++;
		}
		vaddr_idx_start = bit_idx * PAGE_SIZE + cur->userprog_vaddr.vaddr_start;
		ASSERT((uint32_t)vaddr_idx_start < (0xc0000000-PAGE_SIZE));
	}
	
	return (void*)vaddr_idx_start;
}

/*获取虚拟地址对应的页表项地址*/
uint32_t* pte_ptr(uint32_t vaddr) {
	//0xffc00000表示将页目录作为页表使用，(vaddr & 0xffc00000) >> 10在页目录中找到对应的页表
	//PTE_IDX(vaddr)表示获取改地址对应的页表项偏移，最后获取的就是一个页表项的指针(地址)
	//可以通过这个指针指定对应的物理页（虚拟地址和物理地址映射的时候）
	uint32_t* pte = (uint32_t*)((0xffc00000) + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr)*4);
	return pte;
}

uint32_t* pde_ptr(uint32_t vaddr) {
	//0xfffff000表示页目录既是页表，又从页表中找到自己，将页目录当做物理页使用
	//最后获取的就是一个页目录项的指针
	//可以将申请获取的页表物理地址赋予这个指针
	uint32_t* pde = (uint32_t*) (0xfffff000 + PDE_IDX(vaddr)*4 );
	return pde;
}

/*该方法用于获取物理内存的一个物理页*/
static void* palloc(struct pool* m_pool) {
	int phy_idx_start = bitmap_scan(&m_pool->pool_bitmap,1);
	if(phy_idx_start == -1) {
		return NULL;
	}
	bitmap_set(&m_pool->pool_bitmap,phy_idx_start,1);
	uint32_t page_vaddr = phy_idx_start * PAGE_SIZE + m_pool->phy_addr_start;
	return (void*) page_vaddr;
 }


 
 //建立虚拟地址和物理地址的映射
 static void page_table_add(void* _vaddr, void* _page_phyaddr) {
	 
	 uint32_t vaddr = (uint32_t)_vaddr;
	 uint32_t page_phyaddr = (uint32_t) _page_phyaddr;
	 uint32_t* pte = pte_ptr(vaddr);
	 uint32_t* pde = pde_ptr(vaddr);
	 
	 //如果该虚拟地址对应的页表存在
	 if(*pde & 0x00000001) {
		 //判断不存在这个物理页
		 ASSERT(! (*pte & 0x00000001));
		 if(!(*pte & 0x00000001)) {
			 //建立映射
			 *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
		 } else {
			 PANIC("pte build repeate");
			 *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
		 } 
	 } else {
		 //该虚拟地址对应的页表不存在，需要取申请一个页表，页表一律从内核空间中申请
		 uint32_t page_vaddr = (uint32_t) palloc(&kernel_pool);
		 *pde = (page_vaddr | PG_US_U | PG_RW_W | PG_P_1);
		 //新申请的页表必须进行清理，pte是对应的页表项，不加上偏移就是页表的基地址
		 memset((void*)((int)pte & 0xfffff000), 0, PAGE_SIZE);
		 ASSERT(! (*pte&0x00000001));
		 *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
	 }
	 
 }
 
 /*分配pg_cnt个页，成功返回虚拟起始地址，失败返回NULL*/
 void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
	 
	 //1. 获取虚拟首地址
	 ASSERT(pg_cnt > 0 && pg_cnt < 3840);
	 void* vaddr_start = vaddr_get(pf,pg_cnt);
	 
	 if(vaddr_start == NULL) {
		 return NULL;
	 }
	 
	 uint32_t vaddr = (uint32_t) vaddr_start, cnt = pg_cnt;
	 struct pool* m_pool = pf == PF_KERNEL ? &kernel_pool : &user_pool; 
	 //2. 申请物理页
	 while(cnt-- > 0) {
		 void* page_phyaddr = palloc(m_pool);
		 if(page_phyaddr == NULL) {
			 //回滚
			 return NULL;
		 }
		 //建立映射
		 page_table_add((void*)vaddr, page_phyaddr);
		 vaddr += PAGE_SIZE;
	 }
	 return vaddr_start;
 }
 
 void* get_kernel_pages(uint32_t pg_cnt) {
	 
	 void* vaddr_start = malloc_page(PF_KERNEL,pg_cnt);
	 if(vaddr_start == NULL) {
		 return NULL;
	 }
	 return vaddr_start;
 }


/*为用户进程获取pgcnt个页，并建立映射，返回虚拟地址*/
void* get_user_pages(uint32_t pg_cnt){

        lock_acquire(&user_pool.lock);
        void* vaddr = malloc_page(PF_USER,pg_cnt);
        memset(vaddr,0,pg_cnt*PAGE_SIZE);
        lock_release(&user_pool.lock);
        return vaddr;
}

/* assign a actual address for the parmeter vaddr, only support one page*/
void* get_a_page(enum pool_flags pf, uint32_t vaddr) {
	struct pool* mem_pool  = pf & PF_KERNEL ? &kernel_pool : &user_pool;
	lock_acquire(&mem_pool->lock);
	struct task_struct* cur = running_thread();
	int bit_idx = -1;
	if(cur->pgdir != NULL && pf == PF_USER) {
		//获取和虚拟地址vaddr想对应的位图下标
		bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PAGE_SIZE;
		ASSERT(bit_idx > 0);
		//将位图的该下标置1,表示已分配
		bitmap_set(&cur->userprog_vaddr.vaddr_bitmap,bit_idx,1);
	} else if(cur->pgdir == NULL && pf == PF_KERNEL) {
		bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PAGE_SIZE;
		ASSERT(bit_idx > 0);
		bitmap_set(&kernel_vaddr.vaddr_bitmap,bit_idx,1);
	} else {
		PANIC("get_a_page(), not allow kernel alloc userspace or usre alloc kernelsapce bt get_a_page");
	}
	//gain a actual address from mem_pool
	void* page_phyaddr = palloc(mem_pool);
	if(page_phyaddr == NULL) {
		return NULL;
	}
	//via the method build a map between the fllowing address
	page_table_add((void*)vaddr,page_phyaddr);

	//release the lock
	lock_release(&mem_pool->lock);
	return (void*) vaddr;

}

/*gain physcial address for virtual address */
uint32_t addr_v2p(uint32_t vaddr) {
	//get the page table entry address of vaddr
	uint32_t* pte = pte_ptr(vaddr);
	//via pointer gain the physical page base address, then add vaddr's
	//offset address
	return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

/*为fork函数创建的子进程的虚拟位图地址映射物理页*/
void* get_a_page_without_opvaddrbitmap(enum pool_flags pf,
uint32_t vaddr) {
	struct pool* mem_pool = pf & PF_KERNEL ?&kernel_pool : &user_pool;
	lock_acquire(&mem_pool->lock);
	void *page_phyaddr = palloc(mem_pool);
	if(page_phyaddr == NULL) {
		lock_release(&mem_pool->lock);
		return NULL;
	}
	page_table_add((void*)vaddr,page_phyaddr);
	lock_release(&mem_pool->lock);
	return (void*)vaddr;
}

/*初始化内存池*/
static void mem_pool_init(uint32_t all_mem) {
	
	//计算已经使用过的内存used_mem,页目录，第一页和第768到1022页，共256页，再加上内核低端1M内存
	put_str("hero guangge, memory init start!\n");
	uint32_t page_table_size = 256*PAGE_SIZE;
	uint32_t used_mem = page_table_size + 0x00100000;
	uint32_t free_mem = all_mem - used_mem;
	uint32_t all_free_pages = free_mem / PAGE_SIZE;
	//把可用的内存页一分为二，一半给内核，一半给用户进程
	uint32_t kernel_free_pages = all_free_pages / 2;
	uint32_t user_free_pages = all_free_pages - kernel_free_pages;
	
	//获取内核的位图大小，这样获取的位图可能会丢失0-7页，但是就不用做内存的边界检查了
	uint32_t kbmp_length = kernel_free_pages / 8;
	uint32_t ubmp_length = user_free_pages / 8;
	
	//内核的物理起始地址
	uint32_t kp_start = used_mem;
	
	//用户进程的物理起始地址
	uint32_t up_start = used_mem + kernel_free_pages * PAGE_SIZE;
	
	kernel_pool.phy_addr_start = kp_start;
	user_pool.phy_addr_start = up_start;
	
	kernel_pool.pool_size = kernel_free_pages*PAGE_SIZE;
	user_pool.pool_size = user_free_pages*PAGE_SIZE;
	
	kernel_pool.pool_bitmap.btmp_bytes_len = kbmp_length;
	put_str("kernel_pool btmp_bytes_len:");
	put_int(kbmp_length);
	put_str("\n");
	user_pool.pool_bitmap.btmp_bytes_len = ubmp_length;
	put_str("user_pool btmp_bytes_len:");
	put_int(ubmp_length);
	put_str("\n");
	kernel_pool.pool_bitmap.bits = (void*) MEM_BITMAP_BASE;
	user_pool.pool_bitmap.bits = (void*) (MEM_BITMAP_BASE+kbmp_length);
	
	put_str("	kernel_pool_bitmap_start:");
	put_int((uint32_t) kernel_pool.pool_bitmap.bits);
	
	put_str("  kernel_pool_phy_addr_start:");
	put_int(kernel_pool.phy_addr_start);
	put_str("\n");
	
	put_str("	user_pool_bitmap_start:");
	put_int((uint32_t) user_pool.pool_bitmap.bits);
	put_str(" kernel_pool_phy_addr_start:");
	put_int(user_pool.phy_addr_start);
	
	put_str("\n");
	
	bitmap_init(&kernel_pool.pool_bitmap);
	bitmap_init(&user_pool.pool_bitmap);

	//初始化虚拟内存池位图
	kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbmp_length;
	
	kernel_vaddr.vaddr_bitmap.bits = (void*) (MEM_BITMAP_BASE+kbmp_length+ubmp_length);
	kernel_vaddr.vaddr_start = K_HEAP_START;
	bitmap_init(&kernel_vaddr.vaddr_bitmap);
	lock_init(&kernel_pool.lock);
	lock_init(&user_pool.lock);
	put_str("mem_pool_init done\n");
	//0xc009a3c0
	//c000158c T k_thread_a
    //c00016d8 T k_thread_b
}

void block_desc_init(struct mem_block_desc* desc_arr) {
	uint16_t desc_idx, block_size = 16;
	for(desc_idx = 0; desc_idx < DESC_CNT; desc_idx ++) {
		desc_arr[desc_idx].block_size = block_size;
		desc_arr[desc_idx].block_per_arena = (PAGE_SIZE-sizeof(struct arena))/block_size;
		list_init(&desc_arr[desc_idx].free_list);

		block_size *= 2;
	}
}

//return the idx th mem_block in arena
static struct mem_block* arena2block(struct arena* a, uint32_t idx){
	return (struct mem_block*)((uint32_t)a + sizeof(struct arena) + idx*a->desc->block_size);
}

static struct arena* block2arena(struct mem_block* b) {
	return (struct arena*) ((uint32_t)b & 0xfffff000);
}

void* sys_malloc(uint32_t size) {
	enum pool_flags pf;
	struct pool* mem_pool;
	uint32_t pool_size;
	struct mem_block_desc* desc;
	struct task_struct* cur = running_thread();

	if(cur->pgdir == NULL) {
		pf = PF_KERNEL;
		mem_pool = &kernel_pool;
		pool_size = kernel_pool.pool_size;
		desc = k_block_desc;
	} else {
		pf = PF_USER;
		mem_pool = &user_pool;
		pool_size = user_pool.pool_size;
		desc = cur->u_block_desc;
	}

	if(!(size > 0 && size < pool_size)) {
		PANIC("size is illeage!");
		return NULL;
	}

	struct arena* a;
	struct mem_block* b;
	lock_acquire(&mem_pool->lock);
	if(size > 1024) {
		//div up
		uint32_t pg_cnt = DIV_ROUND_UP(size+sizeof(struct arena),PAGE_SIZE);
		a = malloc_page(pf,pg_cnt);
		if(a != NULL) {
			memset(a,0,pg_cnt*PAGE_SIZE);
			a->desc = NULL;
			a->cnt = pg_cnt;
			a->large = true;
			lock_release(&mem_pool->lock);
			return (void*)(a+1);
		} else {
			lock_release(&mem_pool->lock);
			return NULL;
		}
	} else {
		uint8_t desc_idx;

		for(desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
			if(size <= desc[desc_idx].block_size) {
				break;
			}
		}

		if(list_empty(&desc[desc_idx].free_list)) {
			a = malloc_page(pf,1);
			if (a == NULL){
				lock_release(&mem_pool->lock);
				return NULL;
			} 

			memset(a,0,PAGE_SIZE);
			a->desc = &desc[desc_idx];
			a->cnt = desc[desc_idx].block_per_arena;
			a->large = false;

			uint32_t block_idx;
			enum intr_status old_status = intr_disable();

			for(block_idx = 0; block_idx < a->cnt; block_idx++) {
				b = arena2block(a,block_idx);
				ASSERT(!elem_find(&a->desc->free_list,&b->free_elem));
				list_append(&a->desc->free_list,&b->free_elem);
			}
			intr_set_status(old_status);
		}
		b = elem2entry(struct mem_block,free_elem,list_pop(&desc[desc_idx].free_list));
		memset(b,0,desc[desc_idx].block_size);
		a = block2arena(b);
		a->cnt --;
		lock_release(&mem_pool->lock);
		return (void*)b;
	}
}

void mem_init(void) {
	put_str("mem_init start!\n");
	uint32_t mem_bytes_total = (*(uint32_t*)(0xb00));
	mem_pool_init(mem_bytes_total);
	block_desc_init(k_block_desc);
	put_str("mem_init done\n");
}
//recyle the physical address
void pfree(uint32_t pg_phy_addr) {
	struct pool* m_pool;
	uint32_t bit_idx = 0;
	//user physical address
	if(pg_phy_addr >= user_pool.phy_addr_start) {
		bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PAGE_SIZE;
		m_pool = &user_pool; 
	} else {
		bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PAGE_SIZE;
		m_pool = &kernel_pool;
	}
	bitmap_set(&m_pool->pool_bitmap,bit_idx,0);
}

//set the pte's presence bit to 0 of vaddr
static void page_table_pte_remove(uint32_t vaddr) {
	uint32_t* pte = pte_ptr(vaddr);
	*pte &= ~PG_P_1;
	asm volatile("invlpg %0"::"m"(vaddr):"memory");
}

//release pg_cnt virtual pages int the virtual adress poll
static void vaddr_remove(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt){

	uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;
	if(pf == PF_KERNEL) {
		bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PAGE_SIZE;
		while(cnt < pg_cnt) {
			bitmap_set(&kernel_vaddr.vaddr_bitmap,bit_idx_start+cnt,0);
			cnt ++;
		}
	} else {
		struct task_struct* cur  = running_thread();
		bit_idx_start = (vaddr - cur->userprog_vaddr.vaddr_start)/PAGE_SIZE;
		while (cnt < pg_cnt)
		{
			bitmap_set(&cur->userprog_vaddr.vaddr_bitmap,bit_idx_start+cnt,0);
			cnt ++;
		}
		
	}
}

void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
	uint32_t pg_phy_addr;
	uint32_t vaddr = (uint32_t) _vaddr, cnt = 0;
	ASSERT(pg_cnt >= 1 && vaddr % PAGE_SIZE == 0);
	pg_phy_addr = addr_v2p(vaddr);

	ASSERT(pg_phy_addr % PAGE_SIZE == 0 && pg_phy_addr >= 0x102000);
	if(pg_phy_addr >= user_pool.phy_addr_start) {
		vaddr -= PAGE_SIZE;
		while (cnt < pg_cnt){
			vaddr += PAGE_SIZE;
			pg_phy_addr = addr_v2p(vaddr);

			ASSERT(pg_phy_addr%PAGE_SIZE == 0 && pg_phy_addr >= user_pool.phy_addr_start);
			pfree(pg_phy_addr);
			page_table_pte_remove(vaddr);
			cnt ++;
		}
		vaddr_remove(pf,_vaddr,pg_cnt);
	} else {
		vaddr -= PAGE_SIZE;
		while (cnt < pg_cnt){
			vaddr += PAGE_SIZE;
			pg_phy_addr = addr_v2p(vaddr);

			ASSERT(pg_phy_addr%PAGE_SIZE == 0 && pg_phy_addr >= kernel_pool.phy_addr_start && pg_phy_addr <= user_pool.phy_addr_start);
			pfree(pg_phy_addr);
			page_table_pte_remove(vaddr);
			cnt ++;
		}
		vaddr_remove(pf,_vaddr,pg_cnt);
	}
}

void sys_free(void* ptr) {
	ASSERT(ptr != NULL);
	if (ptr != NULL)
	{
		struct pool* mem_pool;
		enum pool_flags pf;
		if(running_thread()->pgdir == NULL) {
			pf = PF_KERNEL;
			mem_pool = &kernel_pool;
		} else {
			pf = PF_USER;
			mem_pool = &user_pool;
		}

		lock_acquire(&mem_pool->lock);

		struct mem_block* b = ptr;
		struct arena* a = block2arena(b);
		ASSERT(a->large == false || a->large == true);
		if(a->desc == NULL && a->large == true) {
			mfree_page(pf,a,a->cnt);
		} else {
			list_append(&a->desc->free_list,&b->free_elem);
			//if all of memory mass is free of the arena,release all mass
			if(++a->cnt == a->desc->block_per_arena) {
				uint32_t block_idx;
				for(block_idx = 0; block_idx < a->desc->block_per_arena; block_idx ++) {
					struct mem_block* b = arena2block(a,block_idx);
					ASSERT(elem_find(&a->desc->free_list,&b->free_elem));
					list_remove(&b->free_elem);
				}
				//release the arena
				mfree_page(pf,a,1);
			}
			
		}
		lock_release(&mem_pool->lock);
	}
	
}



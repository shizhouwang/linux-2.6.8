/*
 * Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 */
#ifndef _LINUX_BOOTMEM_H
#define _LINUX_BOOTMEM_H

#include <asm/pgtable.h>
#include <asm/dma.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/mmzone.h>

/*
 *  simple boot-time physical memory area allocator.
 */

extern unsigned long max_low_pfn;
extern unsigned long min_low_pfn;

/*
 * highest page
 */
extern unsigned long max_pfn;

/*
 * node_bootmem_map is a map pointer - the bits represent all physical 
 * memory pages (including holes) on the node.
 * 1. 每个node包含一个bootmem_data_t；
 * 2. 每个bootmem_data_t中携带给每个node分配内存的相关信息；
 */
typedef struct bootmem_data {
	unsigned long node_boot_start; //节点内存的起始物理地址；
	unsigned long node_low_pfn;    //节点内存对应的低端内存最后一个page的页帧号（代表ZONE_NORMAL结束的页帧号）；
	void *node_bootmem_map;        //指向节点内存对应bitmap所在的虚拟地址；
	unsigned long last_offset;     //针对该页最后一次分配后，相对页尾的偏移，如果该页完全使用，则offset为0；
	unsigned long last_pos;        //最后一次分配使用的页帧号；
	unsigned long last_success;	   //最后一次成功分配的位置；
} bootmem_data_t;

extern unsigned long __init bootmem_bootmap_pages (unsigned long);
extern unsigned long __init init_bootmem (unsigned long addr, unsigned long memend);
extern void __init free_bootmem (unsigned long addr, unsigned long size);
extern void * __init __alloc_bootmem (unsigned long size, unsigned long align, unsigned long goal);
#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
extern void __init reserve_bootmem (unsigned long addr, unsigned long size);
#define alloc_bootmem(x) \
	__alloc_bootmem((x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low(x) \
	__alloc_bootmem((x), SMP_CACHE_BYTES, 0)
#define alloc_bootmem_pages(x) \
	__alloc_bootmem((x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages(x) \
	__alloc_bootmem((x), PAGE_SIZE, 0)
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */
extern unsigned long __init free_all_bootmem (void);

extern unsigned long __init init_bootmem_node (pg_data_t *pgdat, unsigned long freepfn, unsigned long startpfn, unsigned long endpfn);
extern void __init reserve_bootmem_node (pg_data_t *pgdat, unsigned long physaddr, unsigned long size);
extern void __init free_bootmem_node (pg_data_t *pgdat, unsigned long addr, unsigned long size);
extern unsigned long __init free_all_bootmem_node (pg_data_t *pgdat);
extern void * __init __alloc_bootmem_node (pg_data_t *pgdat, unsigned long size, unsigned long align, unsigned long goal);
#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
#define alloc_bootmem_node(pgdat, x) \
	__alloc_bootmem_node((pgdat), (x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_pages_node(pgdat, x) \
	__alloc_bootmem_node((pgdat), (x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages_node(pgdat, x) \
	__alloc_bootmem_node((pgdat), (x), PAGE_SIZE, 0)
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */

extern void *__init alloc_large_system_hash(const char *tablename,
					    unsigned long bucketsize,
					    unsigned long numentries,
					    int scale,
					    int consider_highmem,
					    unsigned int *_hash_shift,
					    unsigned int *_hash_mask);

#endif /* _LINUX_BOOTMEM_H */

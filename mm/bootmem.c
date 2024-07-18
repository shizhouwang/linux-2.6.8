/*
 *  linux/mm/bootmem.c
 *
 *  Copyright (C) 1999 Ingo Molnar
 *  Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 *
 *  simple boot-time physical memory area allocator and
 *  free memory collector. It's used to deal with reserved
 *  system memory and memory holes as well.
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <asm/dma.h>
#include <asm/io.h>

/*
 * Access to this subsystem has to be serialized externally. (this is
 * true for the boot process anyway)
 */
unsigned long max_low_pfn;
unsigned long min_low_pfn;
unsigned long max_pfn;

EXPORT_SYMBOL(max_pfn);		/* This is exported so
				 * dma_get_required_mask(), which uses
				 * it, can be an inline function */

/* return the number of _pages_ that will be allocated for the boot bitmap */
unsigned long __init bootmem_bootmap_pages (unsigned long pages)
{
	unsigned long mapsize;

	mapsize = (pages+7)/8;
	mapsize = (mapsize + ~PAGE_MASK) & PAGE_MASK;
	mapsize >>= PAGE_SHIFT;

	return mapsize;
}

/*
 * Called once to set up the allocator itself.
 */
static unsigned long __init init_bootmem_core (pg_data_t *pgdat,
	unsigned long mapstart, unsigned long start, unsigned long end)
{
	bootmem_data_t *bdata = pgdat->bdata;
	unsigned long mapsize = ((end - start)+7)/8;                         //map单位是byte，所以需要按8取整；

	pgdat->pgdat_next = pgdat_list;
	pgdat_list = pgdat;

	mapsize = (mapsize + (sizeof(long) - 1UL)) & ~(sizeof(long) - 1UL); //保证mapsize的大小是sizeof(long)字节（4）的倍数；
	bdata->node_bootmem_map = phys_to_virt(mapstart << PAGE_SHIFT);      //node_bootmem_map中存放的是map对应的虚拟地址；
	bdata->node_boot_start = (start << PAGE_SHIFT);                      //start对应的是pfn，转换成物理地址；
	bdata->node_low_pfn = end;

	/*
	 * Initially all pages are reserved - setup_arch() has to
	 * register free RAM areas explicitly.
	 * 1. bitmap为1是表示对应的page是保留page
	 */
	memset(bdata->node_bootmem_map, 0xff, mapsize);

	return mapsize;
}

/*
 * Marks a particular physical memory range as unallocatable. Usable RAM
 * might be used for boot-time allocations - or it might get added
 * to the free page pool later on.
 */
static void __init reserve_bootmem_core(bootmem_data_t *bdata, unsigned long addr, unsigned long size)
{
	unsigned long i;
	/*
	 * round up, partially reserved pages are considered
	 * fully reserved.
	 */
	unsigned long sidx = (addr - bdata->node_boot_start)/PAGE_SIZE;
	unsigned long eidx = (addr + size - bdata->node_boot_start + 
							PAGE_SIZE-1)/PAGE_SIZE;
	unsigned long end = (addr + size + PAGE_SIZE-1)/PAGE_SIZE;

	BUG_ON(!size);
	BUG_ON(sidx >= eidx);
	BUG_ON((addr >> PAGE_SHIFT) >= bdata->node_low_pfn);
	BUG_ON(end > bdata->node_low_pfn);

	for (i = sidx; i < eidx; i++)
		if (test_and_set_bit(i, bdata->node_bootmem_map)) {
#ifdef CONFIG_DEBUG_BOOTMEM
			printk("hm, page %08lx reserved twice.\n", i*PAGE_SIZE);
#endif
		}
}

static void __init free_bootmem_core(bootmem_data_t *bdata, unsigned long addr, unsigned long size)
{
	unsigned long i;
	unsigned long start;
	/*
	 * round down end of usable mem, partially free pages are
	 * considered reserved.
	 */
	unsigned long sidx;
	unsigned long eidx = (addr + size - bdata->node_boot_start)/PAGE_SIZE;
	unsigned long end = (addr + size)/PAGE_SIZE;

	BUG_ON(!size);
	BUG_ON(end > bdata->node_low_pfn);

	if (addr < bdata->last_success)
		bdata->last_success = addr;

	/*
	 * Round up the beginning of the address.
	 */
	start = (addr + PAGE_SIZE-1) / PAGE_SIZE;
	sidx = start - (bdata->node_boot_start/PAGE_SIZE);

	for (i = sidx; i < eidx; i++) {
		if (unlikely(!test_and_clear_bit(i, bdata->node_bootmem_map)))
			BUG();
	}
}

/*
 * We 'merge' subsequent allocations to save space. We might 'lose'
 * some fraction of a page if allocations cannot be satisfied due to
 * size constraints on boxes where there is physical RAM space
 * fragmentation - in these cases (mostly large memory boxes) this
 * is not a problem.
 *
 * On low memory boxes we get it right in 100% of the cases.
 *
 * alignment has to be a power of 2 value.
 *
 * NOTE:  This function is _not_ reentrant.
 * 1. 所有bootmem分配器相关内存分配，底层调用的都是此函数；
 * 2. 入参goal表示希望分配内存的起始物理地址（会从这个地址开始查找）；
 */
static void * __init
__alloc_bootmem_core(struct bootmem_data *bdata, unsigned long size,
		unsigned long align, unsigned long goal)
{
	unsigned long offset, remaining_size, areasize, preferred;
	unsigned long i, start = 0, incr, eidx; 
	void *ret;

	if(!size) {
		printk("__alloc_bootmem_core(): zero-sized request\n");
		BUG();
	}
	BUG_ON(align & (align-1));

    //eidx表示在bootmem中页框（page frame）的最大索引号；
	eidx = bdata->node_low_pfn - (bdata->node_boot_start >> PAGE_SHIFT);
	offset = 0;
	//align表示按多少字节对齐：
	if (align &&
	    (bdata->node_boot_start & (align - 1UL)) != 0)
		offset = (align - (bdata->node_boot_start & (align - 1UL)));
	offset >>= PAGE_SHIFT;

	/*
	 * We try to allocate bootmem pages above 'goal'
	 * first, then we try to allocate lower pages.
	 * 如果入参中指定了goal，并且goal对应的物理地址在bootmem对应的内存区间范围内；
	 * 则将preferred设置为相对于内存起始地址的偏移量；
	 * 如果最近一次分配成功的地址偏移量大于preferred，则修改preferred为这个地址偏移量
	 */
	if (goal && (goal >= bdata->node_boot_start) && 
	    ((goal >> PAGE_SHIFT) < bdata->node_low_pfn)) {
		preferred = goal - bdata->node_boot_start;

		if (bdata->last_success >= preferred)
			preferred = bdata->last_success;
	} else
		preferred = 0;
    //将preferred对应的地址按align字节对齐
	preferred = ((preferred + align - 1) & ~(align - 1)) >> PAGE_SHIFT;
	preferred += offset;
	areasize = (size+PAGE_SIZE-1)/PAGE_SIZE;
	//incr表示增量遍历查询的步数；
	//如果align大于一个page的大小，则遍历bootmem的时候按align换算的大小进行计算
	incr = align >> PAGE_SHIFT ? : 1;   

restart_scan:
	for (i = preferred; i < eidx; i += incr) {
		unsigned long j;
		//find_next_zero_bit()函数作用：
		//查询*addr中，从第offset位开始，第一个不为0的位的位数(最低位从0开始);
		//offset最小值为0，最大值为sizeof(unsigned long)*8 - 1
		i = find_next_zero_bit(bdata->node_bootmem_map, eidx, i);
		i = ALIGN(i, incr);
		if (test_bit(i, bdata->node_bootmem_map))
			continue;
	    //从第一个为空的bit位i开始查找，基于需要申请的空间大小，逐个判断后面的bit位是否都不为0（表示可用）；
		for (j = i + 1; j < i + areasize; ++j) {
			if (j >= eidx)
				goto fail_block;
			if (test_bit (j, bdata->node_bootmem_map))
				goto fail_block;
		}
		start = i;
		goto found;
	fail_block:
		i = ALIGN(j, incr);
	}

	if (preferred > offset) {
		preferred = offset;
		goto restart_scan;
	}
	return NULL;

found:
	bdata->last_success = start << PAGE_SHIFT;
	BUG_ON(start >= eidx);

	/*
	 * Is the next page of the previous allocation-end the start
	 * of this allocation's buffer? If yes then we can 'merge'
	 * the previous partial page with this allocation.
	 * 如果align小于PAGE_SIZE并且上一次分配的页框就是这次分配页框的上一个页框并且上一次分配的页框还没用完，则合并前面分配的未用的页框。
	 */
	if (align < PAGE_SIZE &&
	    bdata->last_offset && bdata->last_pos+1 == start) {//上一次分配的页帧号是当前分配的页帧号的前一个；
		offset = (bdata->last_offset+align-1) & ~(align-1);
		BUG_ON(offset > PAGE_SIZE);
		remaining_size = PAGE_SIZE-offset;
		//1. 如果前一个页框剩下未用的大小remaining_size大于这次需要请求分配的空间大小size
		//则直接从上一个页框未使用的空间分配给这次需要的size空间；
		if (size < remaining_size) {
			areasize = 0;
			/* last_pos unchanged */
			bdata->last_offset = offset+size;
			ret = phys_to_virt(bdata->last_pos*PAGE_SIZE + offset +
						bdata->node_boot_start);
		} 
        //2. 如果前一个页框剩下未用的大小remaining_size不大于这次需要请求分配的空间大小size
        //则将请求的一部分分在前一个页框中，另一部分从新计算需要分配的页框数。
		else {
			remaining_size = size - remaining_size;
			areasize = (remaining_size+PAGE_SIZE-1)/PAGE_SIZE;
			ret = phys_to_virt(bdata->last_pos*PAGE_SIZE + offset +
						bdata->node_boot_start);
			bdata->last_pos = start+areasize-1;
			bdata->last_offset = remaining_size;
		}
		bdata->last_offset &= ~PAGE_MASK;
	} else {
		bdata->last_pos = start + areasize - 1;
		bdata->last_offset = size & ~PAGE_MASK;
		ret = phys_to_virt(start * PAGE_SIZE + bdata->node_boot_start);
	}

	/*
	 * Reserve the area now:
	 */
	for (i = start; i < start+areasize; i++)
		if (unlikely(test_and_set_bit(i, bdata->node_bootmem_map)))
			BUG();
	memset(ret, 0, size);
	return ret;
}

/*
 * 1. 所有bootmem分配器相关内存释放，底层调用的都是此函数；
 */
static unsigned long __init free_all_bootmem_core(pg_data_t *pgdat)
{
	struct page *page;
	bootmem_data_t *bdata = pgdat->bdata;
	unsigned long i, count, total = 0;
	unsigned long idx;
	unsigned long *map; 

	BUG_ON(!bdata->node_bootmem_map);

	count = 0;
	/* first extant page of the node */
	page = virt_to_page(phys_to_virt(bdata->node_boot_start));
	idx = bdata->node_low_pfn - (bdata->node_boot_start >> PAGE_SHIFT);
	map = bdata->node_bootmem_map;
	for (i = 0; i < idx; ) {
		unsigned long v = ~map[i / BITS_PER_LONG];
		if (v) {
			unsigned long m;
			for (m = 1; m && i < idx; m<<=1, page++, i++) {
				if (v & m) {
					count++;
					ClearPageReserved(page);
					set_page_count(page, 1);
					__free_page(page);
				}
			}
		} else {
			i+=BITS_PER_LONG;
			page += BITS_PER_LONG;
		}
	}
	total += count;

	/*
	 * Now free the allocator bitmap itself, it's not
	 * needed anymore:
	 */
	page = virt_to_page(bdata->node_bootmem_map);
	count = 0;
	for (i = 0; i < ((bdata->node_low_pfn-(bdata->node_boot_start >> PAGE_SHIFT))/8 + PAGE_SIZE-1)/PAGE_SIZE; i++,page++) {
		count++;
		ClearPageReserved(page);
		set_page_count(page, 1);
		__free_page(page);
	}
	total += count;
	bdata->node_bootmem_map = NULL;

	return total;
}

unsigned long __init init_bootmem_node (pg_data_t *pgdat, unsigned long freepfn, unsigned long startpfn, unsigned long endpfn)
{
	return(init_bootmem_core(pgdat, freepfn, startpfn, endpfn));
}

void __init reserve_bootmem_node (pg_data_t *pgdat, unsigned long physaddr, unsigned long size)
{
	reserve_bootmem_core(pgdat->bdata, physaddr, size);
}

void __init free_bootmem_node (pg_data_t *pgdat, unsigned long physaddr, unsigned long size)
{
	free_bootmem_core(pgdat->bdata, physaddr, size);
}

unsigned long __init free_all_bootmem_node (pg_data_t *pgdat)
{
	return(free_all_bootmem_core(pgdat));
}

#ifndef CONFIG_DISCONTIGMEM
unsigned long __init init_bootmem (unsigned long start, unsigned long pages)
{
	max_low_pfn = pages;
	min_low_pfn = start;
	return(init_bootmem_core(&contig_page_data, start, 0, pages));
}

#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
void __init reserve_bootmem (unsigned long addr, unsigned long size)
{
	reserve_bootmem_core(contig_page_data.bdata, addr, size);
}
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */

void __init free_bootmem (unsigned long addr, unsigned long size)
{
	free_bootmem_core(contig_page_data.bdata, addr, size);
}

unsigned long __init free_all_bootmem (void)
{
	return(free_all_bootmem_core(&contig_page_data));
}
#endif /* !CONFIG_DISCONTIGMEM */

void * __init __alloc_bootmem (unsigned long size, unsigned long align, unsigned long goal)
{
	pg_data_t *pgdat = pgdat_list;
	void *ptr;

	for_each_pgdat(pgdat)
		if ((ptr = __alloc_bootmem_core(pgdat->bdata, size,
						align, goal)))
			return(ptr);

	/*
	 * Whoops, we cannot satisfy the allocation request.
	 */
	printk(KERN_ALERT "bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of memory");
	return NULL;
}

void * __init __alloc_bootmem_node (pg_data_t *pgdat, unsigned long size, unsigned long align, unsigned long goal)
{
	void *ptr;

	ptr = __alloc_bootmem_core(pgdat->bdata, size, align, goal);
	if (ptr)
		return (ptr);

	return __alloc_bootmem(size, align, goal);
}


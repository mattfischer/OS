#include "PageTable.h"

#include "Util.h"

#define PAGE_TABLE_SIZE 4096

#define PAGE_TABLE_SECTION_SIZE (1024 * 1024)
#define PAGE_TABLE_SECTION_MASK 0xfff00000
#define PAGE_TABLE_SECTION_SHIFT 20

#define PTE_TYPE_MASK 3
#define PTE_TYPE_DISABLED 0
#define PTE_TYPE_COARSE 1
#define PTE_TYPE_SECTION 2

#define PTE_SECTION_AP_SHIFT 10
#define PTE_SECTION_AP_READ_WRITE (0x3 << PTE_SECTION_AP_SHIFT)
#define PTE_SECTION_AP_READ_ONLY (0x2 << PTE_SECTION_AP_SHIFT)
#define PTE_SECTION_AP_READ_WRITE_PRIV (0x1 << PTE_SECTION_AP_SHIFT)
#define PTE_SECTION_BASE_MASK 0xfff00000
#define PTE_SECTION_BASE_SHIFT 20

#define PTE_COARSE_BASE_MASK 0xfffffc00
#define PTE_COARSE_BASE_SHIFT 10

#define PAGE_L2_TABLE_SIZE 256

#define PTE_L2_TYPE_MASK 3
#define PTE_L2_TYPE_DISABLED 0
#define PTE_L2_TYPE_LARGE 1
#define PTE_L2_TYPE_SMALL 2

#define PTE_L2_AP0_SHIFT 4
#define PTE_L2_AP1_SHIFT 6
#define PTE_L2_AP2_SHIFT 8
#define PTE_L2_AP3_SHIFT 10


#define PTE_L2_AP_READ_WRITE 0x3
#define PTE_L2_AP_READ_ONLY 0x2
#define PTE_L2_AP_READ_WRITE_PRIV 0x1
#define PTE_L2_AP_ALL_READ_WRITE 0xff0
#define PTE_L2_AP_ALL_READ_ONLY 0xCC0
#define PTE_L2_AP_ALL_READ_WRITE_PRIV 0x550

#define PTE_L2_BASE_MASK 0xfffff000
#define PTE_L2_BASE_SHIFT 12

struct SlabAllocator<struct PageTable> PageTable::sSlab;
struct PageTable *PageTable::Kernel;
static struct Page *kernelTablePages;

extern "C" {
	PAddr KernelTablePAddr;
}

PageTable::PageTable()
{
	int kernel_nr;
	unsigned *base;
	unsigned *kernelTable;

	mPages = Page_AllocContig(4, 4);
	mTablePAddr = PAGE_TO_PADDR(mPages);

	base = (unsigned*)PADDR_TO_VADDR(mTablePAddr);

	kernel_nr = KERNEL_START >> PTE_SECTION_BASE_SHIFT;
	memset(base, 0, kernel_nr * sizeof(unsigned));

	kernelTable = (unsigned*)PADDR_TO_VADDR(Kernel->tablePAddr());
	memcpy(base + kernel_nr, kernelTable + kernel_nr, (PAGE_TABLE_SIZE - kernel_nr) * sizeof(unsigned));
}

PageTable::PageTable(struct Page *pages)
{
	mPages = pages;
	mTablePAddr = PAGE_TO_PADDR(mPages);
	LIST_INIT(mL2Tables);
}

void PageTable::allocL2Table(void *vaddr)
{
	unsigned *table;
	int idx;
	struct Page *L2Page;
	unsigned *L2Table;
	int l2idx;
	unsigned l2pte;
	int i, j;

	table = (unsigned*)PADDR_TO_VADDR(mTablePAddr);
	idx = (unsigned int)vaddr >> PAGE_TABLE_SECTION_SHIFT;

	LIST_FOREACH(mL2Tables, L2Page, struct Page, list) {
		L2Table = (unsigned*)PAGE_TO_VADDR(L2Page);
		for(i=0; i<4; i++) {
			l2idx = i*PAGE_L2_TABLE_SIZE;
			l2pte = L2Table[l2idx];
			if((l2pte & PTE_L2_TYPE_MASK) != PTE_L2_TYPE_DISABLED || (l2pte & 0x80000000) == 0) {
				continue;
			}

			for(j=0; j<PAGE_L2_TABLE_SIZE; j++) {
				L2Table[l2idx + j] = 0;
			}
			table[idx] = VADDR_TO_PADDR(L2Table + l2idx) | PTE_SECTION_AP_READ_WRITE | PTE_TYPE_COARSE;
			return;
		}
	}

	L2Page = Page_Alloc();
	L2Table = (unsigned*)PAGE_TO_VADDR(L2Page);
	LIST_ADD_TAIL(mL2Tables, L2Page->list);

	memset(L2Table, 0, PAGE_L2_TABLE_SIZE * sizeof(unsigned));
	for(i=1; i<4; i++) {
		L2Table[i*PAGE_L2_TABLE_SIZE] = 0x80000000;
	}

	table[idx] = VADDR_TO_PADDR(L2Table) | PTE_TYPE_COARSE;
}

void PageTable::mapPage(void *vaddr, PAddr paddr, Permission permission)
{
	unsigned *table;
	int idx;
	unsigned pte;
	unsigned *L2Table;
	unsigned int perm;
	int l2idx;

	table = (unsigned*)PADDR_TO_VADDR(mTablePAddr);
	idx = (unsigned int)vaddr >> PAGE_TABLE_SECTION_SHIFT;
	pte = table[idx];

	if((pte & PTE_TYPE_MASK) == PTE_TYPE_SECTION ||
	   (pte & PTE_TYPE_MASK) == PTE_TYPE_DISABLED) {
	   allocL2Table(vaddr);
	}

	pte = table[idx];

	switch(permission) {
		case PermissionRO: perm = PTE_L2_AP_ALL_READ_ONLY; break;
		case PermissionRW: perm = PTE_L2_AP_ALL_READ_WRITE; break;
		case PermissionRWPriv: perm = PTE_L2_AP_ALL_READ_WRITE_PRIV; break;
	}

	if((pte & PTE_TYPE_MASK) == PTE_TYPE_COARSE) {
		L2Table = (unsigned*)PADDR_TO_VADDR(pte & PTE_COARSE_BASE_MASK);
		l2idx = ((unsigned)vaddr & (~PAGE_TABLE_SECTION_MASK)) >> PAGE_SHIFT;
		L2Table[l2idx] = (paddr & PTE_COARSE_BASE_MASK) | perm | PTE_L2_TYPE_SMALL;
	}
}

PAddr PageTable::translateVAddr(void *addr)
{
	unsigned *table = (unsigned*)PADDR_TO_VADDR(mTablePAddr);
	int idx = (unsigned)addr >> PAGE_TABLE_SECTION_SHIFT;
	unsigned pte = table[idx];

	if((pte & PTE_TYPE_MASK) == PTE_TYPE_SECTION) {
		return (pte & PTE_SECTION_BASE_MASK) | ((unsigned)addr & (~PTE_SECTION_BASE_MASK));
	}

	unsigned *L2Table = (unsigned*)PADDR_TO_VADDR(pte & PTE_COARSE_BASE_MASK);
	int l2idx = ((unsigned)addr & (~PAGE_TABLE_SECTION_MASK)) >> PAGE_SHIFT;
	return (L2Table[l2idx] & PTE_L2_BASE_MASK) | ((unsigned)addr & (~PTE_L2_BASE_MASK));
}

void PageTable::init()
{
	Kernel = new PageTable(kernelTablePages);
}

SECTION_LOW void PageTable::initLow()
{
	unsigned int vaddr;
	PAddr paddr;
	int i;
	struct Page *pagesLow;
	unsigned *table;
	int idx;
	unsigned int perm;
	struct Page **kernelTablePagesLow;
	PAddr *kernelTablePAddrLow;

	pagesLow = (struct Page*)VADDR_TO_PADDR(Pages);
	for(i=0; i<VADDR_TO_PAGE_NR(__KernelEnd) + 1; i++) {
		pagesLow[i].flags = PAGE_INUSE;
		LIST_ENTRY_CLEAR(pagesLow[i].list);
	}

	kernelTablePagesLow = (struct Page**)VADDR_TO_PADDR(&kernelTablePages);
	*kernelTablePagesLow = Page_AllocContigLow(4, 4);

	kernelTablePAddrLow = (PAddr*)VADDR_TO_PADDR(&KernelTablePAddr);
	*kernelTablePAddrLow = PAGE_TO_PADDR(*kernelTablePagesLow);

	table = (unsigned*)*kernelTablePAddrLow;
	perm = PTE_SECTION_AP_READ_WRITE_PRIV;

	for(vaddr = 0, paddr = 0; vaddr < KERNEL_START; vaddr += PAGE_TABLE_SECTION_SIZE, paddr += PAGE_TABLE_SECTION_SIZE) {
		idx = (unsigned int)vaddr >> PAGE_TABLE_SECTION_SHIFT;
		table[idx] = (paddr & PTE_SECTION_BASE_MASK) | perm | PTE_TYPE_SECTION;
	}

	for(vaddr = KERNEL_START, paddr = 0; vaddr > 0; vaddr += PAGE_TABLE_SECTION_SIZE, paddr += PAGE_TABLE_SECTION_SIZE) {
		idx = (unsigned int)vaddr >> PAGE_TABLE_SECTION_SHIFT;
		table[idx] = (paddr & PTE_SECTION_BASE_MASK) | perm | PTE_TYPE_SECTION;
	}
}
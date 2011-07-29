#include "Page.h"
#include "AddressSpace.h"
#include "Defs.h"

struct Page Pages[N_PAGES];

struct Page *Page_Alloc()
{
	int i;

	for(i=0; i<N_PAGES; i++) {
		struct Page *page = PAGE(i);

		if(page->flags == PAGE_FREE) {
			page->flags = PAGE_INUSE;
			return page;
		}
	}

	return NULL;
}

struct List Page_AllocMulti(int num)
{
	struct List list;
	int n;

	LIST_INIT(list);

	for(n=0; n < num; n++) {
		struct Page *page = Page_Alloc();
		LIST_ADD_TAIL(list, page->list);
	}

	return list;
}

struct List Page_AllocContig(int align, int num)
{
	struct List list;
	int i, j;

	LIST_INIT(list);
	for(i=0; i<N_PAGES; i += align) {
		for(j=0; j<num; j++) {
			struct Page *page = PAGE(i + j);

			if(page->flags == PAGE_INUSE) {
				break;
			}
		}

		if(j == num) {
			for(j=0; j<num; j++) {
				struct Page *page = PAGE(i + j);

				page->flags = PAGE_INUSE;
				LIST_ADD_TAIL(list, page->list);
			}
			return list;
		}
	}

	return list;
}

void Page_Free(struct Page *page)
{
	page->flags = PAGE_FREE;
}

void Page_FreeList(struct List list)
{
	struct Page *page;
	struct Page *extra;

	LIST_FOREACH_CAN_REMOVE(list, page, extra, struct Page, list) {
		LIST_REMOVE(list, page->list);
		Page_Free(page);
	}
}

SECTION_LOW void Page_InitLow()
{
	struct Page *pages = (struct Page*)VADDR_TO_PADDR(Pages);
	int i;

	for(i=0; i<N_PAGES; i++) {
		pages[i].flags = PAGE_FREE;
		LIST_ENTRY_CLEAR(pages[i].list);
	}
}

SECTION_LOW struct List Page_AllocContigLow(int align, int num)
{
	struct List list;
	int i, j;

	LIST_INIT(list);
	for(i=0; i<N_PAGES; i += align) {
		for(j=0; j<num; j++) {
			struct Page *page = PAGE(i + j);
			struct Page *pageLow = (struct Page*)VADDR_TO_PADDR(page);

			if(pageLow->flags == PAGE_INUSE) {
				break;
			}
		}

		if(j == num) {
			for(j=0; j<num; j++) {
				struct Page *page = PAGE(i + j);
				struct Page *pageLow = (struct Page*)VADDR_TO_PADDR(page);

				pageLow->flags = PAGE_INUSE;
				LIST_ADD_TAIL(list, pageLow->list);
			}
			return list;
		}
	}

	return list;
}

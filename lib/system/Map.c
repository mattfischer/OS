#include "include/Map.h"
#include "include/Message.h"

#include <kernel/include/ProcManagerFmt.h>

#include <stddef.h>

void MapPhys(void *vaddr, unsigned int paddr, unsigned int size)
{
	struct ProcManagerMsg msg;

	msg.type = ProcManagerMapPhys;
	msg.u.mapPhys.vaddr = (unsigned int)vaddr;
	msg.u.mapPhys.paddr = paddr;
	msg.u.mapPhys.size = size;
	SendMessage(0, &msg, sizeof(msg), NULL, 0);
}
#include "InitFs.h"
#include "Util.h"

#include <kernel/include/InitFsFmt.h>
#include <kernel/include/NameFmt.h>
#include <kernel/include/IOFmt.h>

#include "Kernel.h"
#include "Object.h"
#include "Message.h"
#include "Sched.h"

#include <lib/shared/include/Name.h>
#include <lib/shared/include/Kernel.h>

// These symbols are populated by the linker script, and point
// to the beginning and end of the initfs data
extern char __InitFsStart[];
extern char __InitFsEnd[];

int fileServer;
Object *InitFs::sNameServer;

// Path at which to register the InitFS
#define PREFIX "/boot"

enum {
	RegisterEvent = SysEventLast
};

// List of registered names, to pass along to real name server
struct RegisteredName : public ListEntry {
	char name[32];
	int obj;
};

Slab<RegisteredName> registeredNameSlab;
List<RegisteredName> registeredNames;

// List of waiters
struct Waiter : public ListEntry {
	int message;
};

Slab<Waiter> waiterSlab;
List<Waiter> waiters;

static void *lookup(const char *name, int *size)
{
	// Search through the initfs searching for a matching file
	struct InitFsFileHeader *header = (struct InitFsFileHeader*)__InitFsStart;
	while((void*)header < (void*)__InitFsEnd) {
		if(!strcmp(header->name, name)) {
			// Filename matches
			if(size) {
				*size = header->size;
			}
			return (char*)header + sizeof(struct InitFsFileHeader);
		}

		// Skip to the next file record
		header = (struct InitFsFileHeader*)((char*)header + sizeof(struct InitFsFileHeader) + header->size);
	}

	return NULL;
}

// Open file information structure
struct FileInfo {
	void *data;
	int size;
	int pointer;
};

Slab<FileInfo> fileInfoSlab;

// InitFS file server.  This also implements a basic proto-name server,
// which is used until the real userspace name server is started.
static void server(void *param)
{
	while(1) {
		union {
			union NameMsg name;
			union IOMsg io;
		} msg;
		int m = Object_Receive(fileServer, &msg, sizeof(msg));

		if(m == 0) {
			switch(msg.name.event.type) {
				case RegisterEvent:
				{
					// The new name server has been started.  Connect this file server
					// into it.
					Name_Set(PREFIX, fileServer);

					// Forward along any registered names to the new name server
					for(RegisteredName *registeredName = registeredNames.head(); registeredName != NULL; registeredName = registeredNames.next(registeredName)) {
						Name_Set(registeredName->name, registeredName->obj);
						Object_Release(registeredName->obj);
					}

					// Fail any waiters so that they will retry the wait with the new name server
					for(Waiter *waiter = waiters.head(); waiter != NULL; waiter = waiters.next(waiter)) {
						Message_Reply(waiter->message, -1, NULL, 0);
					}
					break;
				}
			}
			continue;
		}

		struct MessageInfo info;
		Message_Info(m, &info);

		if(info.targetData == NULL) {
			// Call made to the main file server object
			switch(msg.name.msg.type) {
				case NameMsgTypeOpen:
				{
					int size;
					void *data;
					int obj = OBJECT_INVALID;
					char *name = msg.name.msg.u.open.name;

					if(strncmp(name, PREFIX, strlen(PREFIX)) == 0) {
						// Look up the requested file name in the InitFS
						name += strlen(PREFIX) + 1;
						data = lookup(name, &size);
						if(data) {
							// File was found.  Create an object for the new
							// file and return it
							FileInfo *fileInfo = fileInfoSlab.allocate();
							fileInfo->data = data;
							fileInfo->size = size;
							fileInfo->pointer = 0;
							obj = Object_Create(fileServer, fileInfo);
						}
					}

					struct BufferSegment segs[] = { &obj, sizeof(obj) };
					struct MessageHeader hdr = { segs, 1, 0, 1 };

					Message_Replyx(m, 0, &hdr);
					break;
				}

				case NameMsgTypeSet:
				{
					// This proto-name server will not actually respond to arbitrary
					// name sets, but it wil record them so that it can pass them along
					// to the real name server once it's registered.
					RegisteredName *registeredName = registeredNameSlab.allocate();
					strcpy(registeredName->name, msg.name.msg.u.set.name);
					registeredName->obj = msg.name.msg.u.set.obj;
					registeredNames.addTail(registeredName);
					Message_Reply(m, -1, NULL, 0);
					break;
				}

				case NameMsgTypeWait:
				{
					// Record all waiters, so that we can fail them and restart them
					// against the new name server
					Waiter *waiter = waiterSlab.allocate();
					waiter->message = m;
					waiters.addTail(waiter);
					break;
				}
			}
		} else {
			// Message was sent to an open file handle
			switch(msg.io.msg.type) {
				case IOMsgTypeRead:
				{
					FileInfo *fileInfo = (FileInfo*)info.targetData;
					int size = min(msg.io.msg.u.rw.size, fileInfo->size - fileInfo->pointer);
					Message_Reply(m, size, (char*)fileInfo->data + fileInfo->pointer, size);
					break;
				}

				case IOMsgTypeSeek:
				{
					FileInfo *fileInfo = (FileInfo*)info.targetData;
					fileInfo->pointer = msg.io.msg.u.seek.pointer;
					Message_Reply(m, 0, NULL, 0);
				}
			}
		}
	}
}

/*!
 * \brief Start the InitFS server
 */
void InitFs::start()
{
	fileServer = Object_Create(OBJECT_INVALID, NULL);
	sNameServer = Sched::current()->process()->object(fileServer);

	Task *task = new Task(Kernel::process());
	task->start(server, NULL);
}

/*!
 * \brief Set the name server object
 * \param nameServer New name server
 */
void InitFs::setNameServer(Object *nameServer)
{
	sNameServer = nameServer;

	// Inform the proto-name server that a real name server has been registered
	Kernel::process()->object(fileServer)->post(RegisterEvent, 0);
}

/*!
 * \brief Retrieve the name server object
 * \return Name server
 */
Object* InitFs::nameServer()
{
	return sNameServer;
}
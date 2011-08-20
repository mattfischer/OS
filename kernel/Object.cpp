#include "Object.h"
#include "Slab.h"
#include "Sched.h"
#include "Util.h"

SlabAllocator<Object> Object::sSlab;

Object::Object()
{
}

static int readBuffer(Process *destProcess, void *dest, Process *srcProcess, struct MessageHeader *src, int offset, int size, int translateCache[])
{
	int i, j;
	int copied;
	int srcOffset;

	copied = 0;
	srcOffset = 0;
	for(i=0; i<src->numSegments; i++) {
		struct BufferSegment segment;
		int segmentSize;
		int segmentStart;

		AddressSpace::memcpy(NULL, &segment, srcProcess->addressSpace(), src->segments + i, sizeof(struct BufferSegment));

		if(srcOffset + segment.size < offset) {
			srcOffset += segment.size;
			continue;
		}

		segmentStart = offset + copied - srcOffset;
		segmentSize = min(size - copied, segment.size - segmentStart);

		AddressSpace::memcpy(destProcess->addressSpace(), (char*)dest + copied, srcProcess->addressSpace(), (char*)segment.buffer + segmentStart, segmentSize);

		for(j=0; j<src->objectsSize; j++) {
			int *s, *d;
			int objOffset;
			int obj;

			objOffset = src->objectsOffset + j * sizeof(int);
			if(objOffset < srcOffset + segmentStart || objOffset >= srcOffset + segmentSize) {
				continue;
			}

			s = (int*)PADDR_TO_VADDR(srcProcess->addressSpace()->pageTable()->translateVAddr((char*)segment.buffer + objOffset - srcOffset));
			d = (int*)PADDR_TO_VADDR(destProcess->addressSpace()->pageTable()->translateVAddr((char*)dest + objOffset - offset));
			obj = *s;

			if(translateCache[i] == INVALID_OBJECT) {
				translateCache[i] = destProcess->dupObjectRef(srcProcess, obj);
			}

			*d = translateCache[i];
		}

		srcOffset += segment.size;
		copied += segmentSize;

		if(copied == size) {
			break;
		}
	}

	return copied;
}

static int copyBuffer(Process *destProcess, struct MessageHeader *dest, Process *srcProcess, struct MessageHeader *src, int translateCache[])
{
	int size;
	int copied;
	int i;

	dest->objectsOffset = src->objectsOffset;
	dest->objectsSize = src->objectsSize;

	copied = 0;
	for(i=0; i<dest->numSegments; i++) {
		struct BufferSegment segment;
		int segmentCopied;

		AddressSpace::memcpy(NULL, &segment, destProcess->addressSpace(), dest->segments + i, sizeof(struct BufferSegment));

		segmentCopied = readBuffer(destProcess, segment.buffer, srcProcess, src, copied, segment.size, translateCache);
		copied += segmentCopied;
		if(segmentCopied < segment.size) {
			break;
		}
	}

	return copied;
}

int Object::send(struct MessageHeader *sendMsg, struct MessageHeader *replyMsg)
{
	struct Message message(Current, *sendMsg, *replyMsg);
	struct Task *task;
	int i;

	mMessages.addTail(&message);

	Current->setState(Task::StateSendBlock);

	if(!mReceivers.empty()) {
		task = mReceivers.head();
		mReceivers.remove(task);
		Sched_SwitchTo(task);
	} else {
		Sched_RunNext();
	}

	return message.ret();
}

struct Message *Object::receive(struct MessageHeader *recvMsg)
{
	struct Message *message;
	int size;

	if(mMessages.empty()) {
		mReceivers.addTail(Current);
		Current->setState(Task::StateReceiveBlock);
		Sched_RunNext();
	}

	message = mMessages.head();
	mMessages.remove(message);

	copyBuffer(Current->process(), recvMsg, message->sender()->process(), &message->sendMsg(), message->translateCache());

	message->setReceiver(Current);
	message->sender()->setState(Task::StateReplyBlock);

	return message;
}

Message::Message(Task *sender, struct MessageHeader &sendMsg, struct MessageHeader &replyMsg)
{
	mSender = sender;
	mReceiver = NULL;
	mSendMsg = sendMsg;
	mReplyMsg = replyMsg;
	mRet = 0;

	for(int i=0; i<mSendMsg.objectsSize; i++) {
		mTranslateCache[i] = INVALID_OBJECT;
	}
}

int Message::read(void *buffer, int offset, int size)
{
	return readBuffer(Current->process(), buffer, mSender->process(), &mSendMsg, offset, size, mTranslateCache);
}

int Message::reply(int ret, struct MessageHeader *replyMsg)
{
	int i;
	int translateCache[MESSAGE_MAX_OBJECTS];

	for(i=0; i<replyMsg->objectsSize; i++) {
		translateCache[i] = INVALID_OBJECT;
	}

	copyBuffer(mSender->process(), &mReplyMsg, Current->process(), replyMsg, translateCache);
	mRet = ret;

	Sched_Add(Current);
	Sched_SwitchTo(mSender);

	return 0;
}

int CreateObject()
{
	struct Object *object = new Object();
	return Current->process()->refObject(object);
}

void ReleaseObject(int obj)
{
	Current->process()->unrefObject(obj);
}
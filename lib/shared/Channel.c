#include <Channel.h>

int Channel_Receive(int chan, void *recv, int recvSize, struct MessageInfo *info)
{
	struct BufferSegment recvSegs[] = { recv, recvSize };
	struct MessageHeader recvMsg = { recvSegs, 1, 0, 0 };

	return Channel_Receivex(chan, &recvMsg, info);
}
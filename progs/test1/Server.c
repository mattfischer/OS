#include <Object.h>
#include <Message.h>
#include <Name.h>
#include <Map.h>

#include <stddef.h>

#include "Msg.h"

void PrintUart(char *uart, char *message)
{
	while(*message != '\0') {
		*uart = *message;
		message++;
	}
}

int main(int argc, char *argv[])
{
	char *uart = (char*)0x16000000;
	int obj = CreateObject();

	SetName("test", obj);

	MapPhys(uart, 0x16000000, 4096);
	while(1) {
		struct PrintMsg msg;
		int m;

		m = ReceiveMessage(obj, &msg, sizeof(msg));
		PrintUart(uart, msg.message);
		ReplyMessage(m, 0, NULL, 0);
	}
}
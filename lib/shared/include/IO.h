#ifndef SHARED_IO_H
#define SHARED_IO_H

int File_Open(const char *name);
int File_Write(int obj, void *buffer, int size);

#endif
#ifndef __DISK_H__
#define __DISK_H__
#include <stddef.h>
#include <stdint.h>

#define BLOCK_SIZE 512

void   init_disk();
void   disk_test();
size_t disk_read(size_t block_no, void *buf);
size_t disk_write(size_t block_no, void *buf);
void   disk_flush(void);

#endif
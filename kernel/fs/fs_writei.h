#ifndef __FS_WRITEI_H__
#define __FS_WRITEI_H__

#include <stdint.h>
#include <stddef.h>
#include "fs.h"

// Write data to an inode's blocks from a buffer
int writei(Inode *ip, char *src, uint32_t off, uint32_t n, uint32_t inum);

#endif

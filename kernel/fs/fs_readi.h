#ifndef __FS_READI_H__
#define __FS_READI_H__

#include "fs.h"

// Retrieve an Inode from disk by its inum
void iget(uint32_t inum, Inode *ip_out);

// Write an Inode back to disk
void iupdate(Inode *ip, uint32_t inum);

// Read data from an inode's blocks into a buffer
int readi(Inode *ip, char *dst, uint32_t off, uint32_t n);

#endif

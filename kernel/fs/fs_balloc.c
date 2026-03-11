#include "fs.h"

// Zero a block
static void bzero(uint32_t bno) {
    char buf[BLOCK_SIZE];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        buf[i] = 0;
    }
    disk_write(bno, buf);
}

// Allocate a zeroed disk block.
// Returns 0 if out of disk space.
uint32_t balloc(void) {
    uint32_t b, bi, m;
    char buf[BLOCK_SIZE];

    for(b = 0; b < sb.nblocks; b += BPB) {
        disk_read(BBLOCK(b, sb), buf);
        for(bi = 0; bi < BPB && b + bi < sb.nblocks; bi++) {
            m = 1 << (bi % 8);
            if((buf[bi / 8] & m) == 0) {  // Is block free?
                buf[bi / 8] |= m;         // Mark block in use.
                disk_write(BBLOCK(b, sb), buf); // Write bitmap back
                bzero(b + bi); // Clear new block to prevent returning old data
                return b + bi; // Return the absolute block number
            }
        }
    }
    panic("balloc: out of blocks");
    return 0; 
}

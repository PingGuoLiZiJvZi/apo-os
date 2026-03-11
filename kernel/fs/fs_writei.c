#include "fs_writei.h"

void iupdate(Inode *ip, uint32_t inum);

// Helper writei implementation to abstract writing raw bytes into an inode's data blocks
int writei(Inode *ip, char *src, uint32_t off, uint32_t n, uint32_t inum) {
    uint32_t tot, m;
    uint32_t block_idx;
    uint32_t disk_block = 0;
    char temp_buf[BLOCK_SIZE];
    uint32_t indirect_buf[NINDIRECT];

    if(off > ip->size || off + n < off)
        return -1;
    
    //  MAXFILE
    if(off + n > MAXFILE * BLOCK_SIZE)
        return -1;

    for(tot = 0; tot < n; tot += m, off += m, src += m) {
        block_idx = off / BLOCK_SIZE;
        
        if (block_idx < NDIRECT) {
            // direct block
            if((disk_block = ip->addrs[block_idx]) == 0) {
                disk_block = balloc();
                ip->addrs[block_idx] = disk_block; // bind to file
            }
        } else if (block_idx >= NDIRECT && block_idx < MAXFILE) {
            // == 2. 间接 block ==
            if (ip->addrs[NDIRECT] == 0) {
                // if this is the first time the file uses indirect block, allocate the "pointer array table"
                ip->addrs[NDIRECT] = balloc();
                // at this point, the table is all 0 (bzero has already zeroed it), we need to read it up to prepare for writing new pointers
            }
            
            // whether it was newly allocated or not, we need to get the pointer table into memory
            disk_read(ip->addrs[NDIRECT], (char*)indirect_buf);
            
            disk_block = indirect_buf[block_idx - NDIRECT];
            if (disk_block == 0) {
                disk_block = balloc();
                indirect_buf[block_idx - NDIRECT] = disk_block; 
                
                // write back the updated pointer table to disk
                disk_write(ip->addrs[NDIRECT], (char*)indirect_buf);
            }
        } else {
            // File too large
            panic("writei: file too large");
        }

        // start writing data block
        disk_read(disk_block, temp_buf);
        
        m = BLOCK_SIZE - (off % BLOCK_SIZE);
        if(n - tot < m) {
            m = n - tot;
        }
        
        for(uint32_t i = 0; i < m; i++){
            temp_buf[(off % BLOCK_SIZE) + i] = src[i];
        }

        disk_write(disk_block, temp_buf); 
    }
    
    if(n > 0 || off > ip->size) {
        if(off > ip->size) ip->size = off; 
        iupdate(ip, inum); // update the Inode in memory
    }
    
    return n;
}

#include "fs_writei.h"

void iupdate(Inode *ip, uint32_t inum);

// Helper writei implementation to abstract writing raw bytes into an inode's data blocks
int writei(Inode *ip, char *src, uint32_t off, uint32_t n, uint32_t inum) {
    uint32_t tot, m;
    uint32_t block_idx;
    uint32_t disk_block = 0;
    char temp_buf[BLOCK_SIZE];
    uint32_t indirect_buf[NINDIRECT];
    uint32_t dindirect_buf[NINDIRECT];
    uint32_t tindirect_buf[NINDIRECT];

    if(off > ip->size || off + n < off)
        return -1;
    
    //  MAXFILE
    if(off + n > MAXFILE * BLOCK_SIZE)
        return -1;

    for(tot = 0; tot < n; tot += m, off += m, src += m) {
        block_idx = off / BLOCK_SIZE;
        
        if (block_idx < NDIRECT) {
            if((disk_block = ip->addrs[block_idx]) == 0) {
                disk_block = balloc();
                ip->addrs[block_idx] = disk_block;
            }
        } else if (block_idx < NDIRECT + NINDIRECT) {
            if (ip->addrs[SINDIRECT_IDX] == 0) {
                ip->addrs[SINDIRECT_IDX] = balloc();
            }
            disk_read(ip->addrs[SINDIRECT_IDX], (char*)indirect_buf);
            
            disk_block = indirect_buf[block_idx - NDIRECT];
            if (disk_block == 0) {
                disk_block = balloc();
                indirect_buf[block_idx - NDIRECT] = disk_block; 
                disk_write(ip->addrs[SINDIRECT_IDX], (char*)indirect_buf);
            }
        } else if (block_idx < NDIRECT + NINDIRECT + NDINDIRECT) {
            uint32_t doubly_idx = block_idx - NDIRECT - NINDIRECT;
            uint32_t level1_idx = doubly_idx / NINDIRECT;
            uint32_t level2_idx = doubly_idx % NINDIRECT;

            if (ip->addrs[DINDIRECT_IDX] == 0) {
                ip->addrs[DINDIRECT_IDX] = balloc();
            }

            disk_read(ip->addrs[DINDIRECT_IDX], (char*)dindirect_buf);
            if (dindirect_buf[level1_idx] == 0) {
                dindirect_buf[level1_idx] = balloc();
                disk_write(ip->addrs[DINDIRECT_IDX], (char*)dindirect_buf);
            }

            disk_read(dindirect_buf[level1_idx], (char*)indirect_buf);
            disk_block = indirect_buf[level2_idx];
            if (disk_block == 0) {
                disk_block = balloc();
                indirect_buf[level2_idx] = disk_block;
                disk_write(dindirect_buf[level1_idx], (char*)indirect_buf);
            }
        } else if (block_idx < MAXFILE) {
            uint32_t triply_idx = block_idx - NDIRECT - NINDIRECT - NDINDIRECT;
            uint32_t level1_idx = triply_idx / NDINDIRECT;
            uint32_t rem = triply_idx % NDINDIRECT;
            uint32_t level2_idx = rem / NINDIRECT;
            uint32_t level3_idx = rem % NINDIRECT;

            if (ip->addrs[TINDIRECT_IDX] == 0) {
                ip->addrs[TINDIRECT_IDX] = balloc();
            }

            disk_read(ip->addrs[TINDIRECT_IDX], (char*)tindirect_buf);
            if (tindirect_buf[level1_idx] == 0) {
                tindirect_buf[level1_idx] = balloc();
                disk_write(ip->addrs[TINDIRECT_IDX], (char*)tindirect_buf);
            }

            disk_read(tindirect_buf[level1_idx], (char*)dindirect_buf);
            if (dindirect_buf[level2_idx] == 0) {
                dindirect_buf[level2_idx] = balloc();
                disk_write(tindirect_buf[level1_idx], (char*)dindirect_buf);
            }

            disk_read(dindirect_buf[level2_idx], (char*)indirect_buf);
            disk_block = indirect_buf[level3_idx];
            if (disk_block == 0) {
                disk_block = balloc();
                indirect_buf[level3_idx] = disk_block;
                disk_write(dindirect_buf[level2_idx], (char*)indirect_buf);
            }
        } else {
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

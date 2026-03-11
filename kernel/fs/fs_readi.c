#include "fs_readi.h"
int readi(Inode *ip, char *dst, uint32_t off, uint32_t n) {
    uint32_t tot, m;
    uint32_t block_idx;
    uint32_t disk_block = 0;
    char temp_buf[BLOCK_SIZE];
    uint32_t indirect_buf[NINDIRECT]; // Buffer for indirect pointers

    if(off > ip->size || off + n < off)
        return 0;
    if(off + n > ip->size)
        n = ip->size - off;

    for(tot = 0; tot < n; tot += m, off += m, dst += m) {
        block_idx = off / BLOCK_SIZE;
        
        if (block_idx < NDIRECT) {
            // Direct blocks
            disk_block = ip->addrs[block_idx];
        } else if (block_idx >= NDIRECT && block_idx < NDIRECT + NINDIRECT) {
            // Singly-indirect block
            if (ip->addrs[NDIRECT] == 0) {
                break; // Hole in indirect block pointer
            }
            // Read the indirect block array
            disk_read(ip->addrs[NDIRECT], (char*)indirect_buf);
            // Fetch the actual data block pointer from the array
            disk_block = indirect_buf[block_idx - NDIRECT];
        } else {
            // File too large
            panic("readi: out of singly-indirect blocks");
        }

        if(disk_block == 0) {
            break; // Hole in file
        }
        
        disk_read(disk_block, temp_buf);
        m = BLOCK_SIZE - (off % BLOCK_SIZE);
        if(n - tot < m) {
            m = n - tot;
        }
        
        // Copy to dst
        for(uint32_t i = 0; i < m; i++){
            dst[i] = temp_buf[(off % BLOCK_SIZE) + i];
        }
    }
    return tot;
}

void iget(uint32_t inum, Inode *ip_out) {
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(Inode);
    uint32_t block_no = sb.inode_start + (inum / inodes_per_block); 
    uint32_t offset_in_block = (inum % inodes_per_block) * sizeof(Inode);
    
    char temp_buf[BLOCK_SIZE];
    disk_read(block_no, temp_buf);
    
    Inode *disk_inode = (Inode *)(temp_buf + offset_in_block);
    *ip_out = *disk_inode; // Deep copy
}

// Write the given Inode back to disk (e.g. after its size or addrs[] have been changed)
void iupdate(Inode *ip, uint32_t inum) {
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(Inode);
    uint32_t block_no = sb.inode_start + (inum / inodes_per_block); 
    uint32_t offset_in_block = (inum % inodes_per_block) * sizeof(Inode);
    
    char temp_buf[BLOCK_SIZE];
    
    disk_read(block_no, temp_buf);
    
    Inode *disk_inode = (Inode *)(temp_buf + offset_in_block);
    *disk_inode = *ip; // Deep copy rewrite
    
    disk_write(block_no, temp_buf);
}

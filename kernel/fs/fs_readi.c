#include "fs_readi.h"
int readi(Inode *ip, char *dst, uint32_t off, uint32_t n) {
    uint32_t tot, m;
    uint32_t block_idx;
    uint32_t disk_block = 0;
    char temp_buf[BLOCK_SIZE];
    uint32_t indirect_buf[NINDIRECT];
    uint32_t dindirect_buf[NINDIRECT];
    uint32_t tindirect_buf[NINDIRECT];

    if(off > ip->size || off + n < off)
        return 0;
    if(off + n > ip->size)
        n = ip->size - off;

    for(tot = 0; tot < n; tot += m, off += m, dst += m) {
        block_idx = off / BLOCK_SIZE;
        
        if (block_idx < NDIRECT) {
            disk_block = ip->addrs[block_idx];
        } else if (block_idx < NDIRECT + NINDIRECT) {
            if (ip->addrs[SINDIRECT_IDX] == 0) {
                break;
            }
            disk_read(ip->addrs[SINDIRECT_IDX], (char*)indirect_buf);
            disk_block = indirect_buf[block_idx - NDIRECT];
        } else if (block_idx < NDIRECT + NINDIRECT + NDINDIRECT) {
            if (ip->addrs[DINDIRECT_IDX] == 0) {
                break;
            }

            uint32_t doubly_idx = block_idx - NDIRECT - NINDIRECT;
            uint32_t level1_idx = doubly_idx / NINDIRECT;
            uint32_t level2_idx = doubly_idx % NINDIRECT;

            disk_read(ip->addrs[DINDIRECT_IDX], (char*)dindirect_buf);
            if (dindirect_buf[level1_idx] == 0) {
                break;
            }

            disk_read(dindirect_buf[level1_idx], (char*)indirect_buf);
            disk_block = indirect_buf[level2_idx];
        } else if (block_idx < MAXFILE) {
            if (ip->addrs[TINDIRECT_IDX] == 0) {
                break;
            }

            uint32_t triply_idx = block_idx - NDIRECT - NINDIRECT - NDINDIRECT;
            uint32_t level1_idx = triply_idx / NDINDIRECT;
            uint32_t rem = triply_idx % NDINDIRECT;
            uint32_t level2_idx = rem / NINDIRECT;
            uint32_t level3_idx = rem % NINDIRECT;

            disk_read(ip->addrs[TINDIRECT_IDX], (char*)tindirect_buf);
            if (tindirect_buf[level1_idx] == 0) {
                break;
            }

            disk_read(tindirect_buf[level1_idx], (char*)dindirect_buf);
            if (dindirect_buf[level2_idx] == 0) {
                break;
            }

            disk_read(dindirect_buf[level2_idx], (char*)indirect_buf);
            disk_block = indirect_buf[level3_idx];
        } else {
            panic("readi: file too large");
        }

        if(disk_block == 0) {
            break;
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

#include "fs.h"
#include "../disk/disk.h"
#include "fs_helpers.h"
#include "fs_readi.h"
#include "fs_writei.h"

static File* filealloc(void) {
    for(int i = 0; i < NFILE; i++) {
        if(file_table[i].type == FREE_FILE) { 
            return &file_table[i];
        }
    }
    return 0; // No free file slots
}
void fs_init(){
    // to make it work first,ignore of cache
    // the superblock is in LBA=1 
    char temp_buf[BLOCK_SIZE];
    disk_read(SB_LBA, temp_buf);
    sb = *(SuperBlock *)temp_buf;
    if(sb.magic != MAGIC_NUM){
        panic("Invalid magic number");
    }
}

File* fs_open(const char *path){
    char name[DIRSIZ];
    uint32_t curr_inum = ROOT_INODE; 
    Inode curr_inode;
    
    // get the inode of root dir
    iget(curr_inum, &curr_inode);
    if(curr_inode.type != DIR_INODE){
        panic("Root dir is not a directory");
    }

    // parse the path level by level
    while((path = skipelem(path, name)) != 0) {
        if(curr_inode.type != DIR_INODE) {
            return 0; // failed
        }
        
        uint32_t next_inum = 0;
        DirEntry de;
        
        // use readi to read the dir entry
        for(uint32_t off = 0; off < curr_inode.size; off += sizeof(DirEntry)) {
            if(readi(&curr_inode, (char*)&de, off, sizeof(DirEntry)) != sizeof(DirEntry)) {
                break; 
            }
            if(de.inum == 0) continue; // free/deleted dir entry
            
            // match the name
            int match = 1;
            for(int i = 0; i < DIRSIZ; i++) {
                if(de.name[i] != name[i]) { 
                    match = 0; 
                    break; 
                }
            }
            
            if(match) {
                next_inum = de.inum;
                break;
            }
        }
        
        if(next_inum == 0) {
            return 0; //failed
        }
        
        // update the curr_inode to the next level
        curr_inum = next_inum;
        iget(curr_inum, &curr_inode);
    }
    
    // get the target inode,allocate a file
    File *f = filealloc();
    if(f == 0) return 0; // no free file slots
    f->ref = 1;
    f->inum = curr_inum;
    f->type = curr_inode.type;
    f->off = 0;
    
    // record the name
    for(int i = 0; i < DIRSIZ; i++) {
        f->name[i] = name[i];
    }
    
    return f;
}

void fs_close(File *f){
    if(f == 0) return;
    f->ref--;
    if(f->ref > 0) return;
    f->type = FREE_FILE;
    f->inum = 0;
    f->off = 0;
    for(int i = 0; i < DIRSIZ; i++) {
        f->name[i] = 0;
    }
}

int fs_read(File *f, void *buf, size_t n) {
    if (f == 0 || (f->type != NORMAL_FILE && f->type != DIR_INODE)) {
        return -1;
    }

    Inode ip;
    iget(f->inum, &ip); // Get fresh Inode

    int r = readi(&ip, (char*)buf, f->off, n);
    
    if (r > 0) {
        f->off += r; // Advance file pointer
    }
    
    return r; 
}

int fs_write(File *f, void *buf, size_t n) {
    if (f == 0 || (f->type != NORMAL_FILE && f->type != DIR_INODE)) {
        return -1;
    }

    Inode ip;
    iget(f->inum, &ip); 

    int r = writei(&ip, (char*)buf, f->off, n, f->inum);
    
    if (r > 0) {
        f->off += r; 
    }
    
    return r;
}

int fs_seek(File *f, uint32_t off) {
    if (f == 0) {
        return -1;
    }
    
    f->off = off;
    return 0;
}

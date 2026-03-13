#include "fs.h"
#include "../disk/disk.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../device/device.h"
#include "fs_helpers.h"
#include "fs_readi.h"
#include "fs_writei.h"

SuperBlock sb;
File file_table[NFILE];

static File* filealloc(void) {
    for(int i = 0; i < NFILE; i++) {
        if(file_table[i].type == FREE_FILE) { 
            return &file_table[i];
        }
    }
    return 0; // No free file slots
}
void init_fs(){
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
    char name[DIRSIZ] = {0};
    uint32_t curr_inum = ROOT_INODE; 
    Inode curr_inode;
    int is_device = 0;  // track if path goes through /device/
    
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

        // Check if we are entering the "device" directory
        if (strcmp(name, "device") == 0 && curr_inum == ROOT_INODE) {
            is_device = 1;
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
                if(name[i] == '\0') break; // both strings ended
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
    f->off = 0;

    // Convert inode type to file type
    if (is_device && curr_inode.type == FILE_INODE) {
        f->type = DEVICE_FILE;
    } else if (curr_inode.type == FILE_INODE) {
        f->type = NORMAL_FILE;
    } else if (curr_inode.type == DIR_INODE) {
        f->type = DIR_FILE;
    } else {
        f->type = FREE_FILE;
    }
    
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
    if (f == 0) return -1;

    // Device file: read from UART serial
    if (f->type == DEVICE_FILE) {
        char *p = (char *)buf;
        for (size_t i = 0; i < n; i++) {
            int ch = uart_getchar();
            if (ch < 0) return (int)i;
            p[i] = (char)ch;
        }
        return (int)n;
    }

    if (f->type != NORMAL_FILE && f->type != DIR_FILE) {
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
    if (f == 0) return -1;

    // Device file: write to UART serial
    if (f->type == DEVICE_FILE) {
        const char *p = (const char *)buf;
        for (size_t i = 0; i < n; i++) {
            putchar(p[i]);
        }
        return (int)n;
    }

    if (f->type != NORMAL_FILE && f->type != DIR_FILE) {
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

uint32_t fs_filesize(File *f) {
    if (f == 0) return 0;
    Inode ip;
    iget(f->inum, &ip);
    return ip.size;
}


void fs_test() {
    printf("\n=== FS Test Start ===\n");

    //  Open root directory "/"
    File *root = fs_open("/");
    if (root && root->type == DIR_FILE) {
        printf("[PASS] fs_open(\"/\") -> inum=%d type=%d\n", root->inum, root->type);
    } else {
        printf("[FAIL] fs_open(\"/\") returned NULL or wrong type\n");
        return;
    }
    fs_close(root);

    // Open subdirectory "/device"
    File *dev = fs_open("/device");
    if (dev && dev->type == DIR_FILE) {
        printf("[PASS] fs_open(\"/device\") -> inum=%d\n", dev->inum);
    } else {
        printf("[FAIL] fs_open(\"/device\") failed\n");
    }
    fs_close(dev);

    // Open file "/device/serial" and read its contents
    File *serial = fs_open("/device/serial");
    if (serial && serial->type == DEVICE_FILE) {
        printf("[PASS] fs_open(\"/device/serial\") -> inum=%d\n", serial->inum);
    } else {
        printf("[FAIL] fs_open(\"/device/serial\") failed\n");
        return;
    }

    char buf[128];
    memset(buf, 0, sizeof(buf));
    int n = fs_read(serial, buf, sizeof(buf) - 1);
    if (n > 0) {
        printf("[PASS] fs_read -> %d bytes: \"%s\"\n", n, buf);
    } else {
        printf("[INFO] fs_read -> %d bytes (file may be empty)\n", n);
    }
    fs_close(serial);

    // Write test: open serial, write data, seek back, read it back
    serial = fs_open("/device/serial");
    if (!serial) { printf("[FAIL] re-open serial\n"); return; }

    const char *msg = "APO-OS";
    int w = fs_write(serial, (void*)msg, strlen(msg));
    if (w == (int)strlen(msg)) {
        printf("[PASS] fs_write -> wrote %d bytes\n", w);
    } else {
        printf("[FAIL] fs_write -> %d (expected %d)\n", w, (int)strlen(msg));
    }

    // Seek back to the beginning of what we just wrote
    // (the file had existing content, we wrote after it, so seek to file_size - strlen(msg))
    // Actually easier: just seek to 0 and read everything
    fs_seek(serial, 0);
    memset(buf, 0, sizeof(buf));
    n = fs_read(serial, buf, sizeof(buf) - 1);
    if (n > 0) {
        printf("[PASS] fs_seek(0) + fs_read -> %d bytes: \"%s\"\n", n, buf);
    } else {
        printf("[FAIL] fs_seek + fs_read -> %d\n", n);
    }

    fs_close(serial);

    // Open non-existent path
    File *bad = fs_open("/nonexistent");
    if (bad == 0) {
        printf("[PASS] fs_open(\"/nonexistent\") -> NULL (correct)\n");
    } else {
        printf("[FAIL] fs_open(\"/nonexistent\") should return NULL\n");
        fs_close(bad);
    }

    printf("=== FS Test End ===\n\n");
}
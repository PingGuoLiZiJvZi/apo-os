#include "fs.h"
#include "../disk/disk.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../device/device.h"
#include "fs_helpers.h"
#include "fs_readi.h"
#include "fs_writei.h"

#define MAX_PIPES 16
#define PIPE_BUF_SIZE 16384
#define PIPE_END_READ 1
#define PIPE_END_WRITE 2

typedef struct {
    int used;
    int readers;
    int writers;
    uint32_t rpos;
    uint32_t wpos;
    uint32_t count;
    char buf[PIPE_BUF_SIZE];
} Pipe;

static Pipe pipe_table[MAX_PIPES];

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

static int pipe_alloc(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipe_table[i].used) {
            pipe_table[i].used = 1;
            pipe_table[i].readers = 0;
            pipe_table[i].writers = 0;
            pipe_table[i].rpos = 0;
            pipe_table[i].wpos = 0;
            pipe_table[i].count = 0;
            return i;
        }
    }
    return -1;
}

static void pipe_maybe_free(int pipe_id) {
    if (pipe_id < 0 || pipe_id >= MAX_PIPES) return;
    Pipe *p = &pipe_table[pipe_id];
    if (!p->used) return;
    if (p->readers == 0 && p->writers == 0) {
        p->used = 0;
        p->rpos = p->wpos = p->count = 0;
    }
}

static int pipe_read(File *f, void *buf, size_t n) {
    if (!f || f->pipe_id < 0 || f->pipe_id >= MAX_PIPES) return -1;
    Pipe *p = &pipe_table[f->pipe_id];
    if (!p->used || f->pipe_end != PIPE_END_READ) return -1;
    if (p->count == 0) return 0;

    size_t can = n;
    if (can > p->count) can = p->count;
    char *dst = (char *)buf;
    for (size_t i = 0; i < can; i++) {
        dst[i] = p->buf[p->rpos];
        p->rpos = (p->rpos + 1) % PIPE_BUF_SIZE;
    }
    p->count -= (uint32_t)can;
    return (int)can;
}

static int pipe_write(File *f, const void *buf, size_t n) {
    if (!f || f->pipe_id < 0 || f->pipe_id >= MAX_PIPES) return -1;
    Pipe *p = &pipe_table[f->pipe_id];
    if (!p->used || f->pipe_end != PIPE_END_WRITE) return -1;
    if (p->readers == 0) return -1;

    size_t space = PIPE_BUF_SIZE - p->count;
    if (space == 0) return 0;
    size_t can = n;
    if (can > space) can = space;
    const char *src = (const char *)buf;
    for (size_t i = 0; i < can; i++) {
        p->buf[p->wpos] = src[i];
        p->wpos = (p->wpos + 1) % PIPE_BUF_SIZE;
    }
    p->count += (uint32_t)can;
    return (int)can;
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
    f->pipe_id = -1;
    f->pipe_end = 0;
    
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

    if (f->type == PIPE_FILE && f->pipe_id >= 0 && f->pipe_id < MAX_PIPES) {
        Pipe *p = &pipe_table[f->pipe_id];
        if (f->pipe_end == PIPE_END_READ && p->readers > 0) p->readers--;
        if (f->pipe_end == PIPE_END_WRITE && p->writers > 0) p->writers--;
        pipe_maybe_free(f->pipe_id);
    }

    f->type = FREE_FILE;
    f->inum = 0;
    f->off = 0;
    f->pipe_id = -1;
    f->pipe_end = 0;
    for(int i = 0; i < DIRSIZ; i++) {
        f->name[i] = 0;
    }
}

int fs_read(File *f, void *buf, size_t n) {
    if (f == 0) return -1;

    if (f->type == PIPE_FILE) {
        return pipe_read(f, buf, n);
    }

    if (f->type == DEVICE_FILE) {
        return device_fs_read(f->name, &f->off, buf, n);
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

    if (f->type == PIPE_FILE) {
        return pipe_write(f, buf, n);
    }

    if (f->type == DEVICE_FILE) {
        return device_fs_write(f->name, &f->off, buf, n);
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
    if (f->type == PIPE_FILE) return -1;
    
    f->off = off;
    return 0;
}

uint32_t fs_filesize(File *f) {
    if (f == 0) return 0;
    if (f->type == PIPE_FILE) return 0;
    Inode ip;
    iget(f->inum, &ip);
    return ip.size;
}

File *fs_dup(File *f) {
    if (!f) return 0;
    if (f->type == PIPE_FILE && f->pipe_id >= 0 && f->pipe_id < MAX_PIPES) {
        Pipe *p = &pipe_table[f->pipe_id];
        if (p->used) {
            if (f->pipe_end == PIPE_END_READ) p->readers++;
            if (f->pipe_end == PIPE_END_WRITE) p->writers++;
        }
    }
    f->ref++;
    return f;
}

int fs_pipe_create(File **read_end, File **write_end) {
    if (!read_end || !write_end) return -1;

    int pipe_id = pipe_alloc();
    if (pipe_id < 0) return -1;

    File *r = filealloc();
    if (!r) {
        pipe_table[pipe_id].used = 0;
        return -1;
    }
    r->ref = 1;
    r->type = PIPE_FILE;
    r->inum = 0;
    r->off = 0;
    r->pipe_id = pipe_id;
    r->pipe_end = PIPE_END_READ;
    memset(r->name, 0, sizeof(r->name));

    File *w = filealloc();
    if (!w) {
        r->type = FREE_FILE;
        r->ref = 0;
        r->pipe_id = -1;
        r->pipe_end = 0;
        pipe_table[pipe_id].used = 0;
        return -1;
    }
    w->ref = 1;
    w->type = PIPE_FILE;
    w->inum = 0;
    w->off = 0;
    w->pipe_id = pipe_id;
    w->pipe_end = PIPE_END_WRITE;
    memset(w->name, 0, sizeof(w->name));

    pipe_table[pipe_id].readers = 1;
    pipe_table[pipe_id].writers = 1;

    *read_end = r;
    *write_end = w;
    return 0;
}

int fs_stat_file(File *f, Stat *st) {
    if (!f || !st) return -1;

    if (f->type == PIPE_FILE) {
        st->inum = 0;
        st->type = FILE_INODE;
        st->size = 0;
        st->nlink = 1;
        return 0;
    }

    Inode ip;
    iget(f->inum, &ip);
    st->inum = f->inum;
    st->type = ip.type;
    st->size = ip.size;
    st->nlink = ip.nlink;
    return 0;
}

int fs_stat_path(const char *path, Stat *st) {
    if (!path || !st) return -1;
    File *f = fs_open(path);
    if (!f) return -1;
    int ret = fs_stat_file(f, st);
    fs_close(f);
    return ret;
}

int fs_ioctl(File *f, uint64_t req, uint64_t arg) {
    (void)arg;
    if (!f) return -1;
    if (f->type == DEVICE_FILE) return 0;
    if (f->type == PIPE_FILE && req == 0x541B) {
        if (f->pipe_id < 0 || f->pipe_id >= MAX_PIPES) return -1;
        return (int)pipe_table[f->pipe_id].count;
    }
    return -1;
}

int fs_poll_file(File *f, int events) {
    if (!f) return 0;
    int revents = 0;
    const int POLLIN = 0x001;
    const int POLLOUT = 0x004;

    if (f->type == PIPE_FILE) {
        if (f->pipe_id < 0 || f->pipe_id >= MAX_PIPES) return 0;
        Pipe *p = &pipe_table[f->pipe_id];
        if ((events & POLLIN) && f->pipe_end == PIPE_END_READ && p->count > 0) {
            revents |= POLLIN;
        }
        if ((events & POLLOUT) && f->pipe_end == PIPE_END_WRITE && p->count < PIPE_BUF_SIZE) {
            revents |= POLLOUT;
        }
        return revents;
    }

    if (events & POLLIN) revents |= POLLIN;
    if (events & POLLOUT) revents |= POLLOUT;
    return revents;
}

uint64_t fs_mmap_size(File *f) {
    if (!f) return 0;
    if (f->type == DEVICE_FILE) return device_mmap_size(f->name);
    return 0;
}

int fs_mmap_page(File *f, uint64_t offset, uint64_t *pa) {
    if (!f || !pa) return -1;
    if (f->type == DEVICE_FILE) return device_mmap_page(f->name, offset, pa);
    return -1;
}

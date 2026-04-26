#ifndef __FS_H__
#define __FS_H__
#include <stdint.h>
#include <stddef.h>
#include "../system/system.h"
#include "../disk/disk.h"

#define MAGIC_NUM 0x1919810
#define FREE_INODE 0
#define DIR_INODE 1
#define FILE_INODE 2
#define FREE_FILE 0
#define NORMAL_FILE 1
#define DEVICE_FILE 2
#define DIR_FILE 3
#define PIPE_FILE 4
#define NFILE 100
#define BLOCK_SIZE 512
#define ADDRS_PER_INODE 12
#define DIRSIZ 28
#define NDIRECT 9
#define NINDIRECT (BLOCK_SIZE / sizeof(uint32_t))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define NTINDIRECT (NDINDIRECT * NINDIRECT)
#define SINDIRECT_IDX NDIRECT
#define DINDIRECT_IDX (NDIRECT + 1)
#define TINDIRECT_IDX (NDIRECT + 2)
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT + NTINDIRECT)
#define SB_LBA 1
#define ROOT_INODE 1

#define BPB (BLOCK_SIZE * 8) // Bits per block (4096)
// Block containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmap_start)

// EVERY THING IS A FILE!!!
typedef struct SuperBlock{
    uint32_t magic;
    uint32_t ninodes;
    uint32_t nblocks;
    // Im too lazy to implement log
    // uint32_t nlog;
    // uint32_t log_start;
    uint32_t inode_start;
    uint32_t bmap_start; // Data block bitmap start block
} SuperBlock;

extern SuperBlock sb;

typedef struct Inode{
    uint16_t type;
    uint16_t nlink;
    uint32_t size;
    uint32_t addrs[ADDRS_PER_INODE];
} Inode;

typedef struct DirEntry{
    uint32_t inum;
    char name[DIRSIZ];
} DirEntry;

typedef struct File{
    uint32_t ref;
    uint32_t inum;
    uint32_t off;
    uint16_t type;
    int pipe_id;
    uint8_t pipe_end;
    char name[DIRSIZ];
} File;

// Global file table 

extern File file_table[NFILE];

typedef struct {
    uint32_t inum;       
    uint16_t type;       
    uint32_t size;       
    uint16_t nlink;      
} Stat;

void init_fs();

File* fs_open(const char *path);             
void fs_close(File *f);                     
int fs_read(File *f, void *buf, size_t n); 
int fs_write(File *f, void *buf, size_t n);
int fs_seek(File *f, uint32_t off);
uint32_t fs_filesize(File *f);
File *fs_dup(File *f);
int fs_pipe_create(File **read_end, File **write_end);
int fs_stat_file(File *f, Stat *st);
int fs_stat_path(const char *path, Stat *st);
int fs_ioctl(File *f, uint64_t req, uint64_t arg);
int fs_poll_file(File *f, int events);
uint64_t fs_mmap_size(File *f);
int fs_mmap_page(File *f, uint64_t offset, uint64_t *pa);

// File* fs_create(const char *path, uint16_t type);
// int fs_unlink(const char *path);      

// int fs_stat(File *f, Stat *st);   
// int fs_readdir(File *dir, char *name, uint32_t *inum); 

uint32_t balloc(void); // Allocate a zeroed disk block
#endif

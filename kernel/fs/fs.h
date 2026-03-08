#ifndef __FS_H__
#define __FS_H__
#include <stdint.h>
#include <stddef.h>

typedef struct File File;

void fs_init();

File* fs_open(const char *path);             
void fs_close(File *f);                     
int fs_read(File *f, void *buf, size_t n); 
int fs_write(File *f, void *buf, size_t n);
int fs_seek(File *f, uint32_t off);       

File* fs_create(const char *path, uint16_t type);
int fs_unlink(const char *path);      

typedef struct {
    uint32_t inum;       
    uint16_t type;       
    uint32_t size;       
    uint16_t nlink;      
} Stat;

int fs_stat(File *f, Stat *st);   
int fs_readdir(File *dir, char *name, uint32_t *inum); 
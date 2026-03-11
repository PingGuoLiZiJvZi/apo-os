#include "fs_helpers.h"

const char* skipelem(const char *path, char *name) {
    const char *s;
    int len;

    while(*path == '/')
        path++;
    if(*path == 0)
        return 0;
    
    s = path;
    while(*path != '/' && *path != 0)
        path++;
    
    len = path - s;
    if(len >= DIRSIZ) {
        // Truncate name if it exceeds the maximum size of DirEntry.name (DIRSIZ - 1 for null terminator)
        for(int i = 0; i < DIRSIZ - 1; i++)
            name[i] = s[i];
        name[DIRSIZ - 1] = 0;
    } else {
        for(int i = 0; i < len; i++)
            name[i] = s[i];
        name[len] = 0;
    }

    while(*path == '/')
        path++;
    
    return path;
}


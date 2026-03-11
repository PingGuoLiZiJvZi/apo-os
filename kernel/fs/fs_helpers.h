#ifndef __FS_HELPERS_H__
#define __FS_HELPERS_H__
#include "fs.h"
// Helper routine to skip leading slashes and extract the next directory element into 'name'.
// Returns a pointer to the remaining path.
const char* skipelem(const char *path, char *name);

#endif

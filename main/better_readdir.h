// better_readdir() for fatfs by chipweinberger 
// https://github.com/espressif/esp-idf/issues/10220#issuecomment-1323230460
// eliminates the need to call stat() to determine file size

#pragma once

#include "dirent.h"

#ifdef __cplusplus
extern "C" {
#endif

int better_vfs_fat_readdir_r(void* ctx, DIR* pdir,
        struct dirent* entry, struct dirent** out_dirent, FILINFO* out_fileinfo);
struct dirent* better_vfs_fat_readdir(void* ctx, DIR* pdir, FILINFO* out_fileinfo);

#ifdef __cplusplus
}
#endif
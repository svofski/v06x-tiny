// better_readdir() for fatfs by chipweinberger 
// https://github.com/espressif/esp-idf/issues/10220#issuecomment-1323230460
// eliminates the need to call stat() to determine file size

#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "dirent.h"
#include "errno.h"

#include "params.h"

// clone of the secret internal structure
typedef struct {
    DIR dir;
    long offset;
    FF_DIR ffdir;
    FILINFO filinfo;
    struct dirent cur_dirent;
} vfs_fat_dir_t;

static int fresult_to_errno(FRESULT fr)
{
    switch(fr) {
        case FR_DISK_ERR:       return EIO;
        case FR_INT_ERR:        return EIO;
        case FR_NOT_READY:      return ENODEV;
        case FR_NO_FILE:        return ENOENT;
        case FR_NO_PATH:        return ENOENT;
        case FR_INVALID_NAME:   return EINVAL;
        case FR_DENIED:         return EACCES;
        case FR_EXIST:          return EEXIST;
        case FR_INVALID_OBJECT: return EBADF;
        case FR_WRITE_PROTECTED: return EACCES;
        case FR_INVALID_DRIVE:  return ENXIO;
        case FR_NOT_ENABLED:    return ENODEV;
        case FR_NO_FILESYSTEM:  return ENODEV;
        case FR_MKFS_ABORTED:   return EINTR;
        case FR_TIMEOUT:        return ETIMEDOUT;
        case FR_LOCKED:         return EACCES;
        case FR_NOT_ENOUGH_CORE: return ENOMEM;
        case FR_TOO_MANY_OPEN_FILES: return ENFILE;
        case FR_INVALID_PARAMETER: return EINVAL;
        case FR_OK: return 0;
    }
    assert(0 && "unhandled FRESULT");
    return ENOTSUP;
}

// returns the FILEINFO as well!
int better_vfs_fat_readdir_r(void* ctx, DIR* pdir,
        struct dirent* entry, struct dirent** out_dirent, FILINFO* out_fileinfo)
{
    assert(pdir);
    vfs_fat_dir_t* fat_dir = (vfs_fat_dir_t*) pdir;
    FRESULT res = f_readdir(&fat_dir->ffdir, &fat_dir->filinfo);
    if (res != FR_OK) {
        *out_dirent = NULL;
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        return fresult_to_errno(res);
    }

    // copy fileinfo to output
    *out_fileinfo = fat_dir->filinfo;

    if (fat_dir->filinfo.fname[0] == 0) {
        // end of directory
        *out_dirent = NULL;
        return 0;
    }
    entry->d_ino = 0;
    if (fat_dir->filinfo.fattrib & AM_DIR) {
        entry->d_type = DT_DIR;
    } else {
        entry->d_type = DT_REG;
    }
    strlcpy(entry->d_name, fat_dir->filinfo.fname,
            sizeof(entry->d_name));
    fat_dir->offset++;
    *out_dirent = entry;
    return 0;
}

// returns the FILEINFO as well!
struct dirent* better_vfs_fat_readdir(void* ctx, DIR* pdir, FILINFO* out_fileinfo)
{
    vfs_fat_dir_t* fat_dir = (vfs_fat_dir_t*) pdir;
    struct dirent* out_dirent;
    int err = better_vfs_fat_readdir_r(ctx, pdir, &fat_dir->cur_dirent, &out_dirent, out_fileinfo);
    if (err != 0) {
        errno = err;
        return NULL;
    }
    return out_dirent;
}


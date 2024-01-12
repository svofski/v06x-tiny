#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cctype>
#include <filesystem>

#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "dirent.h"
#include "unistd.h"
#include "sys/stat.h"
#include "fcntl.h"

#include "util.h"
#include "params.h"

#include "better_readdir.h"
#include "psram_allocator.h"

#define MOUNT_POINT_SD "/sdcard"
#define V06C_DIR "/vector06"

extern bool sdcard_busy;

struct FileInfo 
{
    std::string name;
    std::string fullpath;
    ssize_t size;

    std::string size_string() const {
        if (size < 9999) {
            return std::to_string(size);
        }
        else {
            return std::to_string(size/1024) + "K";
        }
    }

    char initial() const
    {
        return name.length() ? toupper(name.at(0)) : 0;
    }
};


enum AssetKind 
{
    AK_UNKNOWN = -1,
    AK_ROM = 0,
    AK_WAV,
    AK_FDD,
    AK_EDD,
    AK_BAS,
    AK_LAST = AK_BAS
};

struct Blob
{
    AssetKind kind;
    std::vector<uint8_t, PSRAMAllocator<uint8_t>> bytes;

    std::vector<uint8_t> std_bytes() {
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    }
};

struct AssetStorage
{
    std::array<std::vector<FileInfo, PSRAMAllocator<FileInfo>>,AK_BAS-AK_ROM+1> files;

    static AssetKind guess_kind(std::string path) {
        std::string ext = util::str_tolower_copy(std::filesystem::path(path).extension());
        
        if (ext == ".rom" || ext == ".r0m" || ext == ".vec") return AK_ROM;
        if (ext == ".wav") return AK_WAV;
        if (ext == ".fdd") return AK_FDD;
        if (ext == ".edd") return AK_EDD;
        if (ext == ".bas" || ext == ".asc") return AK_BAS;

        return AK_UNKNOWN;
    }

    void clear()
    {
        for (auto n = 0; n < files.size(); ++n) {
            files[n].clear();
        }
    }

    static AssetKind prev(AssetKind k)
    {
        int ik = (int)k;
        if (--ik < 0) {
            ik = (int)AK_LAST;
        }
        return (AssetKind)ik;
    }

    static AssetKind next(AssetKind k)
    {
        int ik = (int)k;
        if (++ik > AK_LAST) {
            ik = 0;
        }
        return (AssetKind)ik;
    }

    static const char* asset_cstr(AssetKind k)
    {
        const char* s = "???";
        switch (k) {
            case AK_ROM:
                s = "ROM";
                break;
            case AK_WAV:
                s = "WAV";
                break;
            case AK_FDD:
                s = "FDD";
                break;
            case AK_EDD:
                s = "EDD";
                break;
            case AK_BAS:
                s = "BAS";
                break;
            default:
                break;
        }

        return s;
    }
};

enum {
    SD_GET_FILESIZE,
    SD_LOAD_FILE,
};

struct SDRequest
{
    uint8_t request;
    uint32_t param1;
    uint32_t param2;
};

class SDCard 
{
private:
    sdmmc_card_t * card;
    AssetStorage storage;
    QueueHandle_t request_queue;

public:
    QueueHandle_t osd_notify_queue;
    Blob blob;

    SDCard() : card(nullptr)
    {
        request_queue = xQueueCreate(64, sizeof(SDRequest));
        osd_notify_queue = xQueueCreate(64, sizeof(int));
    }

    bool mount()
    {
        esp_err_t ret;
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 8,
            .allocation_unit_size = 16 * 1024};

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.flags |= SDMMC_HOST_FLAG_1BIT;
        host.max_freq_khz = SDCARD_FREQ_KHZ;

        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.host_id = SPI2_HOST;
        slot_config.gpio_cs = static_cast<gpio_num_t>(PIN_NUM_SDCARD_SS);

        ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT_SD, &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK) {
            printf("%s: could not mount sdcard\n", __PRETTY_FUNCTION__);
        }
        return ret == ESP_OK;
    }

    void unmount()
    {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT_SD, card);
    }

    ssize_t get_filesize(const std::string& path)
    {
        struct stat st;
        for (int nretries = 0; nretries < SDCARD_NRETRIES; ++nretries) {
            if (stat(path.c_str(), &st) == 0) {
                return st.st_size;
            }
            vTaskDelay(10);
        }
        printf("get_filesize(): gave up for %s\n", path.c_str());
        return -1;
    }

    ssize_t load_blob(const FileInfo * fi)
    {
        ssize_t result = -1;

        printf("load_blob: %s size=%d\n", fi->fullpath.c_str(), fi->size);

        blob.kind = AssetStorage::guess_kind(fi->name);
        blob.bytes.clear();

        //sdcard_busy = true; // can't do it like this because we could be in the middle of keyboard transaction?

        keyboard::osd_takeover(true);

        int fd = open(fi->fullpath.c_str(), O_RDONLY);
        if (fd != -1) {
            blob.bytes.resize(fi->size);
            #if READ_BLOBS_IN_BLOCKS
            constexpr int BLOCK_SZ = 4096;
            result = 0;
            while (result < blob.bytes.size()) {
                size_t bytes_read = read(fd, blob.bytes.data() + result, BLOCK_SZ);
                result += bytes_read;
                if (bytes_read < BLOCK_SZ) {
                    break;
                }
                putchar('.');
                vTaskDelay(1);
            }
            #else
            result = read(fd, blob.bytes.data(), blob.bytes.size());
            #endif
        }
        close(fd);

        keyboard::osd_takeover(false);
        //sdcard_busy = false;

        printf("load_blob: done, %d bytes read\n", result);

        return result;
    }

    void rescan_storage()
    {
        storage.clear();

        if (card == nullptr) {
            printf("%s: not mounted\n", __PRETTY_FUNCTION__);
            return;
        }

        std::string root {MOUNT_POINT_SD "/vector06"};
        
        static std::vector<std::string> dirs;
        dirs.push_back(root);

        while (dirs.size() > 0) {
            printf("remaining dirs: %d\n", dirs.size());
            std::string dirpath = dirs.back();
            DIR *dir = opendir(dirpath.c_str());
            dirs.pop_back();

            if (dir == nullptr) {
                printf("%s: could not open " MOUNT_POINT_SD V06C_DIR "\n", __PRETTY_FUNCTION__);
                continue;
            }

            printf("Reading directory: %s\n", dirpath.c_str());
            #if USE_BETTER_READDIR
            FILINFO filinfo{};
            dirent *dent = better_vfs_fat_readdir(NULL, dir, &filinfo);
            #else
            dirent *dent = readdir(dir);
            #endif
            for (; dent != nullptr;) {
                if (dent->d_type == DT_REG) {
                    std::string name{dent->d_name};
                    AssetKind kind = AssetStorage::guess_kind(name);
                    if (kind != AK_UNKNOWN) {
                        std::string path{dirpath + '/' + name};
                        FileInfo fi{
                            .name = name,
                            .fullpath = path,
                            #if USE_BETTER_READDIR
                            .size = static_cast<ssize_t>(filinfo.fsize)
                            #else
                            .size = get_filesize(path)
                            #endif 
                        };

                        storage.files[kind].emplace_back(fi);
                        //printf("%s %s %s\n", fi.fullpath.c_str(), fi.name.c_str(), fi.size_string().c_str());
                    }
                }
                else if (dent->d_type == DT_DIR) {
                    std::string subdir = dirpath + "/" + std::string{dent->d_name};
                    dirs.push_back(subdir); // push into dirs to read later
                }
                #if USE_BETTER_READDIR
                dent = better_vfs_fat_readdir(NULL, dir, &filinfo);
                #else
                dent = readdir(dir);
                #endif
            }
            closedir(dir);
        }

        for (auto kind = 0; kind < storage.files.size(); ++kind) {
            std::sort(storage.files[kind].begin(), storage.files[kind].end(), 
                [](const FileInfo& a, const FileInfo& b) {
                    return util::str_tolower_copy(a.name) < util::str_tolower_copy(b.name);
                });
        }

        printf("rescan_storage: ROM: %d WAV: %d FDD: %d EDD: %d BAS: %d\n",
            get_file_count(AK_ROM),
            get_file_count(AK_WAV),
            get_file_count(AK_FDD),
            get_file_count(AK_EDD),
            get_file_count(AK_BAS)
            );
    }

    void create_pinned_to_core()
    {
        xTaskCreatePinnedToCore(&SDCard::sdcard_task, "sdcard", 1024*4, this, SDCARD_PRIORITY, NULL, SDCARD_CORE);
    }

    static void sdcard_task(void * _self)
    {
        SDCard * self = reinterpret_cast<SDCard *>(_self);
        while (!self->mount()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        self->rescan_storage();

        while (true) {
            SDRequest r;
            xQueueReceive(self->request_queue, &r, portMAX_DELAY);
            switch (r.request) {
                case SD_GET_FILESIZE:
                    {
                        FileInfo * fi = const_cast<FileInfo *>(self->get_file_info((AssetKind)r.param1, r.param2));
                        if (fi != nullptr) {
                            fi->size = self->get_filesize(fi->fullpath);
                            if (fi->size == -1) {
                                fi->size = -3;
                            }
                            int arg = 1;
                            xQueueSend(self->osd_notify_queue, &arg, 0);
                        }
                    }
                    break;
                case SD_LOAD_FILE:
                    {
                        FileInfo * fi = const_cast<FileInfo *>(self->get_file_info((AssetKind)r.param1, r.param2));
                        if (fi != nullptr) {
                            int result = self->load_blob(fi);
                            xQueueSend(self->osd_notify_queue, &result, 0);
                        }
                    }
                    break;
            }
        }
    }

    void request_filesize(AssetKind kind, int index)
    {
        FileInfo *fi = const_cast<FileInfo *>(get_file_info(kind, index));
        fi->size = -2; 
        SDRequest r;
        r.request = SD_GET_FILESIZE;
        r.param1 = (int)kind;
        r.param2 = index;
        xQueueSend(request_queue, &r, portMAX_DELAY);
    }

    void load_asset(AssetKind kind, int index)
    {
        const FileInfo * fi = get_file_info(kind, index);
        if (fi == nullptr) {
            return;
        }
        SDRequest r;
        r.request = SD_LOAD_FILE;
        r.param1 = (int)kind;
        r.param2 = index;
        xQueueSend(request_queue, &r, portMAX_DELAY);
    }

    const FileInfo* get_file_info(AssetKind kind, int index)
    {
        if (kind != AK_UNKNOWN && index >= 0 && index < storage.files[kind].size()) {
            FileInfo * info = &storage.files[kind][index];
            return info;
        }
        return nullptr;
    }

    int get_file_count(AssetKind kind) const 
    {
        if (kind == AK_UNKNOWN)
            return 0;
        return storage.files[kind].size();
    }
};
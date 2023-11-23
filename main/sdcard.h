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
//#include "sys/stat.h"
//#include "sys/types.h"

#include "params.h"

#define MOUNT_POINT_SD "/sdcard"
#define V06C_DIR "/vector06"

struct FileInfo 
{
    std::string name;
    std::string fullpath;
    ssize_t size;

    std::string size_string() const {
        if (size < 9999) {
            return ::to_string(size);
        }
        else {
            return ::to_string(size/1024) + "K";
        }
    }
};

enum AssetKind 
{
    AK_UNKNOWN = -1,
    AK_ROM = 0,
    AK_WAV,
    AK_FDD,
    AK_EDD,
    AK_BAS
};

std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), 
                   [](unsigned char c){ return std::tolower(c); } // correct
                  );
    return s;
}

struct AssetStorage
{
    std::array<std::vector<FileInfo>,AK_BAS-AK_ROM+1> files;

    static AssetKind guess_kind(std::string path) {
        std::string ext = str_tolower(std::filesystem::path(path).extension());
        
        if (ext == ".rom" || ext == ".r0m" || ext == ".vec") return AK_ROM;
        if (ext == ".wav") return AK_WAV;
        if (ext == ".fdd") return AK_FDD;
        if (ext == ".edd") return AK_EDD;
        if (ext == ".bas" || ext == ".asc") return AK_BAS;

        return AK_UNKNOWN;
    }
};

class SDCard 
{
private:
    sdmmc_card_t * card;

    AssetStorage storage;

public:
    SDCard() : card(nullptr)
    {
    }

    bool mount()
    {
        esp_err_t ret;
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 8,
            .allocation_unit_size = 16 * 1024};

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
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

    ssize_t get_filesize(const std::string& path)
    {
        struct stat st;
        for (int nretries = 0; nretries < SDCARD_NRETRIES; ++nretries) {
            if (stat(path.c_str(), &st) == 0) {
                return st.st_size;
            }
            vTaskDelay(1);
        }
        return -1;
    }

    void test()
    {
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
            dirent *dent = readdir(dir);
            for (; dent != nullptr;) {
                if (dent->d_type == DT_REG) {
                    std::string name{dent->d_name};
                    AssetKind kind = AssetStorage::guess_kind(name);
                    if (kind != AK_UNKNOWN) {
                        std::string path{dirpath + '/' + name};
                        FileInfo fi{
                            .name = name,
                            .fullpath = path,
                            .size = -1 //get_filesize(path)
                        };

                        storage.files[kind].emplace_back(fi);
                        printf("%s %s %s\n", fi.fullpath.c_str(), fi.name.c_str(), fi.size_string().c_str());
                    }
                }
                else if (dent->d_type == DT_DIR) {
                    std::string subdir = dirpath + "/" + std::string{dent->d_name};
                    dirs.push_back(subdir); // push into dirs to read later
                }
                dent = readdir(dir);
            }
            closedir(dir);
        }
        printf("read_dirs done\n");
    }

    void create_pinned_to_core()
    {
        xTaskCreatePinnedToCore(&SDCard::sdcard_task, "sdcard", 1024*4, this, SDCARD_PRIORITY, NULL, SDCARD_CORE);
    }

    static void sdcard_task(void * _self)
    {
        extern bool osd_showing;

        SDCard * self = reinterpret_cast<SDCard *>(_self);
        keyboard::osd_takeover(true);
        self->mount();
        self->test();
        keyboard::osd_takeover(osd_showing);
        vTaskDelete(NULL);
    }
};
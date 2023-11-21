#pragma once

#include <cstdint>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "dirent.h"

#include "params.h"

#define MOUNT_POINT_SD "/sdcard"
#define V06C_DIR "/vector06"

class SDCard 
{
private:
    sdmmc_card_t * card;

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
        host.max_freq_khz = 400;

        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.host_id = SPI2_HOST;
        slot_config.gpio_cs = static_cast<gpio_num_t>(PIN_NUM_SDCARD_SS);

        ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT_SD, &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK) {
            printf("%s: could not mount sdcard\n", __PRETTY_FUNCTION__);
        }
        return ret == ESP_OK;
    }

    void test()
    {
        if (card == nullptr) {
            printf("%s: not mounted\n", __PRETTY_FUNCTION__);
            return;
        }

        DIR * dir = opendir(MOUNT_POINT_SD "/vector06");
        if (dir == nullptr) {
            printf("%s: could not open " MOUNT_POINT_SD V06C_DIR "\n", __PRETTY_FUNCTION__);
            return;
        }
        dirent * dent = readdir(dir);
        for (;dent != nullptr;) {
            printf("%s\n", dent->d_name);
            dent = readdir(dir);
        }
        closedir(dir);
    }
};
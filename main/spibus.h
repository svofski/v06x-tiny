#pragma once
#include <cstdint>
#include "driver/spi_master.h"
#include "params.h"

// just a wrapper to initialise SPI bus on SPI2_HOST shared by keyboard and sdcard
class SPIBus
{
public:
    SPIBus()
    {
        spi_bus_config_t buscfg = {
            .mosi_io_num = PIN_NUM_MOSI,
            .miso_io_num = PIN_NUM_MISO,
            .sclk_io_num = PIN_NUM_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000, // 4 bytes
        };

        esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
        ESP_ERROR_CHECK(ret);
    }

    SPIBus(const SPIBus&) = delete;
};
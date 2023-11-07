#include "keyboard.h"


// keyboard protocol:
// 32-bit transaction (MSB, mode 0)
//  0:  TX port 03 (PA) value, column select        RX: blk, vvod
//  1:                                              RX: 00
//  2:  TX port 01 (PC) value, ruslat led           RX: bits 5,6,7 modkeys SS,US,RUSLAT
//  3:                                              RX: rows

namespace keyboard
{

keyboard_state_t state;

std::function<void(bool)> onreset;

static bool transaction_active;
static spi_device_handle_t spimatrix;

static spi_transaction_t transaction;

void init()
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4, // 4 bytes
    };

    spi_device_interface_config_t devcfg = {
        .mode = 0, // SPI mode 0 (CPOL=0, CPHA=0)
        .clock_speed_hz = 12000000,
        .spics_io_num = PIN_NUM_KEYBOARD_SS,
        .queue_size = 1,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spimatrix);
    ESP_ERROR_CHECK(ret);

    transaction_active = false;
    transaction = {};
}

void select_columns(uint8_t pa, uint8_t pc)
{
    if (transaction_active) {
        update_state();
    }
    transaction.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
    transaction.tx_data[0] = pa; 
    transaction.tx_data[2] = pc; // ruslat led in bit 3 (0 == on)
    transaction.length = 32;
    esp_err_t ret = spi_device_polling_start(spimatrix, &transaction, portMAX_DELAY);
    transaction_active = ret == ESP_OK;
}

void update_state()
{
    if (transaction_active) {
        esp_err_t ret = spi_device_polling_end(spimatrix, 0);
        if (ret == ESP_OK) {
            state.rows = transaction.rx_data[3];
            state.pc = transaction.rx_data[2] & PC_MDOKEYS_MASK;
            transaction_active = false;
        }
    }
}

}